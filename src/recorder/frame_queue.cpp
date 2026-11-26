#include "recorder/ingest.hpp"

#include <algorithm>

namespace bacuri::vms::ingest
{

FrameQueue::FrameQueue(Profile profile, const std::vector<FramePtr>& group) : FrameQueue{profile}
{
    if (group.empty() || !group.front()->keyframe)
        return;
    for (const auto& frame : group) {
        frames_.push_back(frame);
        stats_.bytes += frame->access_unit.size();
    }
    group_frames_ = frames_.size();
    group_bytes_ = stats_.bytes;
    stats_.peak_bytes = stats_.bytes;
    stats_.peak_depth = frames_.size();
    need_keyframe_ = false;
}

void FrameQueue::push(const FramePtr& frame)
{
    std::scoped_lock lock{mutex_};
    if (frames_.size() >= capacity_.frames + group_frames_ ||
        frame->access_unit.size() > capacity_.bytes + group_bytes_ - stats_.bytes) {
        if (!stats_.loss_begin || stats_.recovered_at) {
            stats_.loss_begin = profile_ == Profile::secondary && !frames_.empty()
                                    ? frames_.front()->time.received_at()
                                    : frame->time.received_at();
        }
        stats_.recovered_at.reset();
        need_keyframe_ = true;
        if (profile_ == Profile::main) {
            ++stats_.dropped;
            return;
        }
        stats_.dropped += frames_.size();
        frames_.clear();
        stats_.bytes = 0;
        group_frames_ = group_bytes_ = 0;
    }
    if ((need_keyframe_ && !frame->keyframe) || frame->access_unit.size() > capacity_.bytes) {
        ++stats_.dropped;
        return;
    }
    if (need_keyframe_ && stats_.loss_begin && !stats_.recovered_at) {
        stats_.recovered_at = frame->time.received_at();
    }
    need_keyframe_ = false;
    frames_.push_back(frame);
    stats_.bytes += frame->access_unit.size();
    stats_.peak_bytes = std::max(stats_.peak_bytes, stats_.bytes);
    stats_.peak_depth = std::max(stats_.peak_depth, frames_.size());
    ready_.notify_one();
}

void FrameQueue::interrupt()
{
    std::scoped_lock lock{mutex_};
    need_keyframe_ = true;
}

FramePtr FrameQueue::try_pop()
{
    std::scoped_lock lock{mutex_};
    return pop_locked();
}

FramePtr FrameQueue::wait_pop(std::chrono::steady_clock::time_point deadline)
{
    std::unique_lock lock{mutex_};
    ready_.wait_until(lock, deadline, [this] { return !frames_.empty(); });
    return pop_locked();
}

FramePtr FrameQueue::pop_locked()
{
    if (frames_.empty()) {
        return {};
    }
    auto frame = std::move(frames_.front());
    frames_.pop_front();
    stats_.bytes -= frame->access_unit.size();
    if (group_frames_) {
        --group_frames_;
        group_bytes_ -= std::min(group_bytes_, frame->access_unit.size());
    }
    return frame;
}

QueueStats FrameQueue::stats() const
{
    std::scoped_lock lock{mutex_};
    auto copy = stats_;
    copy.depth = frames_.size();
    return copy;
}

} // namespace bacuri::vms::ingest
