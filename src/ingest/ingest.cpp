#include "ingest/rtsp_session.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace bacuri::vms::ingest
{
using namespace std::chrono_literals;

Stream::Stream(StreamOptions options)
    : options_{std::move(options)}, worker_{[this](const std::stop_token& stop) { run(stop); }}
{
}

Stream::~Stream()
{
    request_stop();
}

void Stream::request_stop()
{
    worker_.request_stop();
}

std::shared_ptr<FrameQueue> Stream::subscribe()
{
    std::scoped_lock lock{mutex_};
    // A new consumer receives the current group at once instead of waiting for the next keyframe.
    auto queue = std::make_shared<FrameQueue>(options_.profile, group_);
    std::erase_if(consumers_, [](const auto& entry) { return entry.expired(); });
    consumers_.push_back(queue);
    return queue;
}

StreamStats Stream::stats() const
{
    std::scoped_lock lock{mutex_};
    auto copy = stats_;
    for (const auto& consumer : consumers_) {
        if (auto queue = consumer.lock()) {
            copy.consumers.push_back(queue->stats());
        }
    }
    return copy;
}

void Stream::report() const
{
    if (options_.report) {
        options_.report(stats());
    }
}

void Stream::interrupt(std::string state)
{
    std::scoped_lock lock{mutex_};
    if (stats_.state == "receiving" && stats_.time) {
        ++stats_.gaps;
        stats_.gap_begin = stats_.time->received_at() + 1us;
        stats_.gap_end.reset();
        for (const auto& consumer : consumers_) {
            if (auto queue = consumer.lock()) {
                queue->interrupt();
            }
        }
    }
    group_.clear();
    group_bytes_ = 0;
    stats_.state = std::move(state);
}

void Stream::remember(const FramePtr& frame)
{
    const auto capacity = limits(Profile::main);
    if (frame->keyframe) {
        group_.clear();
        group_bytes_ = 0;
    } else if (group_.empty()) {
        return;
    }
    if (group_.size() < capacity.frames && group_bytes_ + frame->access_unit.size() <= capacity.bytes) {
        group_.push_back(frame);
        group_bytes_ += frame->access_unit.size();
    } else {
        group_.clear();
        group_bytes_ = 0;
    }
}

void Stream::receive(const FramePtr& frame, VideoFormat format, bool discontinuity)
{
    if (discontinuity) {
        interrupt("discontinuity");
    }
    bool recovered{};
    {
        std::scoped_lock lock{mutex_};
        if (stats_.state != "receiving") {
            if (!frame->keyframe) {
                return;
            }
            recovered = true;
            stats_.state = "receiving";
            if (stats_.gap_begin && !stats_.gap_end) {
                stats_.gap_end = std::max(*stats_.gap_begin, frame->time.received_at());
            }
        }
        ++stats_.frames;
        stats_.bytes += frame->access_unit.size();
        stats_.keyframes += frame->keyframe;
        stats_.time = frame->time;
        stats_.rtp_timestamp = frame->rtp_timestamp;
        stats_.ntp_ns = frame->ntp_ns;
        stats_.pts_ns = frame->pts_ns;
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - sample_at_).count();
        if (recovered || elapsed >= 1000) {
            if (!recovered)
                stats_.bitrate_kbps = static_cast<unsigned>((stats_.bytes - sample_bytes_) * 8 /
                                                            static_cast<std::uint64_t>(elapsed));
            sample_at_ = now;
            sample_bytes_ = stats_.bytes;
        }
        stats_.codec = frame->codec;
        stats_.width = format.width;
        stats_.height = format.height;
        remember(frame);
        std::erase_if(consumers_, [&frame](const auto& entry) {
            if (auto queue = entry.lock()) {
                queue->push(frame);
                return false;
            }
            return true;
        });
    }
    if (recovered || discontinuity) {
        report();
    }
}

void Stream::run(const std::stop_token& stop)
{
    using Steady = std::chrono::steady_clock;
    auto delay = 1s;
    auto next_report = Steady::now() + 2s;
    while (!stop.stop_requested()) {
        std::optional<Steady::time_point> first_frame;
        auto on_frame = [&](const FramePtr& frame, VideoFormat format, bool discontinuity) {
            if (!first_frame) {
                first_frame = Steady::now();
            }
            receive(frame, format, discontinuity);
            if (Steady::now() >= next_report) {
                report();
                next_report = Steady::now() + 2s;
            }
        };
        std::string reason = "address_denied";
        if (auto url = options_.stream_url(options_.endpoint.url)) {
            const Endpoint target{*url, options_.endpoint.username, options_.endpoint.password};
            reason = receive_rtsp(target, stop, on_frame);
        }
        if (stop.stop_requested()) {
            break;
        }
        interrupt(std::move(reason));
        report();
        if (first_frame && Steady::now() - *first_frame >= 5s) {
            delay = 1s;
        }
        std::unique_lock wait{mutex_};
        retry_.wait_for(wait, stop, delay, [] { return false; });
        if (!stop.stop_requested()) {
            ++stats_.reconnects;
        }
        delay = std::min(delay * 2, 30s);
    }
}

} // namespace bacuri::vms::ingest
