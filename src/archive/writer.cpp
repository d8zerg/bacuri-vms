#include "archive/writer.hpp"
#include "archive/fmp4.hpp"
#include "observability/metrics.hpp"
#include "recorder/replay.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <utility>
#include <thread>

namespace bacuri::vms::archive
{
namespace
{
using namespace std::chrono_literals;
using Frames = std::vector<ingest::FramePtr>;
// Buffered event and manual recording is published early when it exceeds this size.
constexpr std::size_t max_event_buffer_bytes = 128uz * 1024 * 1024;
// Longest wait for a frame before the writer checks the recording policy and the stop request.
constexpr auto policy_check_interval = 100ms;
// A block without new frames for this long is closed so that its recording becomes visible.
constexpr auto idle_block_timeout = 5s;
constexpr std::uint64_t second_ns = 1'000'000'000, microsecond_ns = 1'000;

std::uint64_t duration(const Frames& frames, std::size_t index)
{
    const auto& frame = *frames[index];
    if (auto value = frame.duration_ns; value && *value > 0 && *value <= second_ns) {
        return *value;
    }
    if (frames.size() > 1) {
        const auto i = std::min(index, frames.size() - 2);
        const auto a = frames[i]->pts_ns, b = frames[i + 1]->pts_ns;
        if (a && b && *b > *a && *b - *a <= second_ns) {
            return *b - *a;
        }
    }
    // Only the observed microsecond is known when a single frame has no duration.
    return microsecond_ns;
}

struct SampleTime {
    std::uint64_t pts, duration;
};

std::vector<SampleTime> sample_times(const Frames& frames)
{
    const auto base = frames.front()->pts_ns;
    std::uint64_t cursor{};
    std::vector<SampleTime> result;
    result.reserve(frames.size());
    for (std::size_t i = 0; i < frames.size(); ++i) {
        auto pts = frames[i]->pts_ns;
        const auto offset = base && pts && *pts >= *base && *pts - *base <= 15 * second_ns ? *pts - *base : cursor;
        result.push_back({offset, duration(frames, i)});
        cursor = offset + result.back().duration;
    }
    return result;
}

void mux(const fs::path& path, const Frames& frames, const std::vector<SampleTime>& times)
{
    std::vector<fmp4::Sample> samples;
    samples.reserve(frames.size());
    for (std::size_t i = 0; i < frames.size(); ++i)
        samples.push_back({frames[i]->access_unit, times[i].pts, times[i].duration, frames[i]->keyframe});
    const auto media = fmp4::encode(
        frames.front()->codec == ingest::VideoCodec::h265 ? fmp4::Codec::h265 : fmp4::Codec::h264, samples);
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    file.write(reinterpret_cast<const char*>(media.data()), static_cast<std::streamsize>(media.size()));
    file.close();
    if (!file)
        throw std::runtime_error("Cannot write archive media");
}
bool buffers_events(const CaptureRule& rule)
{
    return !rule.continuous && (rule.manual || rule.event);
}
std::chrono::seconds event_buffer(const CaptureRule& rule)
{
    return std::chrono::seconds{rule.event ? rule.pre : 0} + event_buffer_margin;
}
} // namespace

std::chrono::microseconds publication_lag(const CaptureRule* rule)
{
    std::chrono::microseconds lag = max_block_duration;
    if (rule && buffers_events(*rule))
        lag += event_buffer(*rule);
    return lag;
}

ReplayResult recover_gap(Volume& volume, const ReplayRequest& job, const std::function<bool()>& cancelled)
{
    using namespace domain;
    std::stop_source stop;
    ReplayResult result;
    Frames frames;
    std::size_t bytes{};
    const auto started = std::chrono::steady_clock::now();
    auto eligible = started;
    const auto parsed_epoch = ClockEpochId::parse(uuid());
    if (!parsed_epoch)
        throw std::runtime_error("Invalid replay clock identifier");
    const auto epoch = *parsed_epoch;
    // Set when the time budget ended the copy and when a frame at or after the requested end arrived.
    bool exhausted = false, reached_end = false;
    const auto flush = [&] {
        if (frames.empty())
            return;
        auto times = sample_times(frames);
        const auto first = frames.front()->time.utc().time_since_epoch().count();
        const auto end =
            frames.back()->time.utc().time_since_epoch().count() +
            std::max<std::int64_t>(1, static_cast<std::int64_t>(times.back().duration / microsecond_ns));
        if (first >= job.begin && end <= job.end && end > first &&
            volume.publish(
                {job.origin, frames.front()->time, UtcTime{Microseconds{end}},
                 frames.front()->codec == ingest::VideoCodec::h265 ? "h265" : "h264", job.recording},
                [&](const fs::path& path) { mux(path, frames, times); }, {}, bytes)) {
            ++result.blocks;
            result.duration_us += end - first;
            result.covered_until_us = std::max(result.covered_until_us, end);
        }
        frames.clear();
        bytes = 0;
    };
    const auto outcome = ingest::receive_replay(
        job.endpoint, {job.begin - job.offset, job.end - job.offset},
        [&] { return cancelled() || stop.stop_requested(); },
        [&](const ingest::FramePtr& incoming, ingest::VideoFormat, bool discontinuity) {
            if (cancelled()) {
                stop.request_stop();
                return;
            }
            if (std::chrono::steady_clock::now() - started > replay_time_budget) {
                exhausted = true;
                stop.request_stop();
                return;
            }
            // Pace the replay reader at 4 Mbit/s (2 us per byte). Socket backpressure bounds the decoder queue.
            constexpr std::size_t pacing_us_per_byte = 2;
            eligible = std::max(eligible, std::chrono::steady_clock::now()) +
                       std::chrono::microseconds{pacing_us_per_byte * incoming->access_unit.size()};
            if (eligible > started + replay_time_budget) {
                exhausted = true;
                result.outcome = "quota";
                stop.request_stop();
                return;
            }
            while (!cancelled() && std::chrono::steady_clock::now() < eligible)
                std::this_thread::sleep_until(std::min(eligible, std::chrono::steady_clock::now() + 20ms));
            if (cancelled())
                return;
            if (discontinuity || !incoming->ntp_ns) {
                frames.clear();
                bytes = 0;
            }
            constexpr std::int64_t ntp_offset = 2208988800000000LL;
            if (!incoming->ntp_ns)
                return;
            const SourceTime source{Microseconds{static_cast<std::int64_t>(*incoming->ntp_ns / 1000)}};
            const auto utc = UtcTime{source.value - Microseconds{ntp_offset} + Microseconds{job.offset}};
            if (utc.time_since_epoch().count() >= job.end) {
                reached_end = true;
                flush();
                stop.request_stop();
                return;
            }
            if (utc.time_since_epoch().count() < job.begin)
                return;
            if (!frames.empty() && (utc <= frames.back()->time.utc() || utc - frames.back()->time.utc() > 1s)) {
                frames.clear();
                bytes = 0;
            }
            if (incoming->keyframe)
                flush();
            if (frames.empty() && !incoming->keyframe)
                return;
            if (frames.size() >= 512 || bytes + incoming->access_unit.size() > 16uz * 1024 * 1024) {
                frames.clear();
                bytes = 0;
                return;
            }
            auto frame = std::make_shared<ingest::Frame>(*incoming);
            frame->time = ProjectedTime::from_source({epoch, source, utc, Microseconds{job.uncertainty}}, source,
                                                     incoming->time.received_at())
                              .value();
            frame->pts_ns = static_cast<std::uint64_t>(utc.time_since_epoch().count()) * 1000;
            bytes += frame->access_unit.size();
            frames.push_back(frame);
        });
    if (!cancelled())
        flush();
    // Copied media is never reported as the whole interval when the budget ended the copy early.
    if (result.blocks && exhausted && !reached_end)
        result.outcome = "partial";
    else if (result.outcome.empty())
        result.outcome = result.blocks ? "copied" : outcome == "frame_timeout" ? "timeout" : "no_frames";
    return result;
}

Writer::Writer(Volume& volume, Origin origin, std::shared_ptr<ingest::FrameQueue> queue,
               std::shared_ptr<ingest::CameraAudio> audio)
    : volume_{volume}, origin_{std::move(origin)}, queue_{std::move(queue)}, audio_{std::move(audio)},
      worker_{[this](const std::stop_token& stop) { run(stop); }}
{
}

void Writer::finish()
{
    if (frames_.empty()) {
        return;
    }
    auto frames = std::exchange(frames_, {});
    auto bytes = std::exchange(bytes_, 0);
    if (!rule_)
        return;
    auto sound = audio_ && rule_->audio != RecordingAudio::disabled
                     ? audio_->recorded(frames.front()->time.received_at().time_since_epoch().count(),
                                        frames.back()->time.utc() - frames.front()->time.utc() +
                                            std::chrono::ceil<domain::Microseconds>(
                                                std::chrono::nanoseconds{duration(frames, frames.size() - 1)}))
                     : ingest::Sound{};
    buffered_bytes_ += bytes;
    buffered_.push_back({std::move(frames), bytes, std::move(sound), rule_->id, rule_->revision});
    drain(false);
}
void Writer::drain(bool stopping)
{
    if (!rule_)
        return;
    const auto now = std::chrono::time_point_cast<domain::Microseconds>(std::chrono::system_clock::now());
    while (!buffered_.empty()) {
        const auto& item = buffered_.front();
        const auto& frames = item.frames;
        const auto age = now - frames.back()->time.received_at();
        const bool matching = item.policy == rule_->id && item.revision == rule_->revision;
        if (matching && !stopping && buffers_events(*rule_) && age < event_buffer(*rule_) &&
            buffered_bytes_ <= max_event_buffer_bytes)
            break;
        auto segment = std::move(buffered_.front());
        buffered_.pop_front();
        buffered_bytes_ -= segment.bytes;
        if (!matching)
            continue;
        std::size_t first = segment.frames.size(), last = 0;
        for (std::size_t i = 0; i < segment.frames.size(); ++i) {
            if (rule_->selects(segment.frames[i]->time.utc().time_since_epoch().count(), origin_.binding)) {
                first = std::min(first, i);
                last = i + 1;
            }
        }
        if (last)
            publish(std::move(segment), first, last);
    }
}
void Writer::publish(Segment segment, std::size_t first, std::size_t last)
{
    auto& batch = segment.frames;
    auto times = sample_times(batch);
    batch.resize(last);
    times.resize(last);
    const auto payload_bytes = segment.bytes;
    std::uint64_t length{};
    for (const auto& sample : times) {
        length = std::max(length, sample.pts + sample.duration);
    }
    const auto end =
        batch.front()->time.utc() + std::chrono::ceil<domain::Microseconds>(std::chrono::nanoseconds{length});
    const auto begin_us = batch.front()->time.utc().time_since_epoch().count();
    const auto end_us = end.time_since_epoch().count();
    try {
        if (storage_gap_) {
            volume_.report_gap(*storage_gap_);
            storage_gap_.reset();
        }
        const auto received = batch.front()->time.received_at().time_since_epoch().count();
        auto& sound = segment.audio;
        auto start = begin_us + (sound.start_us - received);
        // Audio may only cover a contiguous selection, even when decoding video needs its preceding keyframe.
        std::size_t audio_end = first + 1;
        while (audio_end < last &&
               rule_->selects(batch[audio_end]->time.utc().time_since_epoch().count(), origin_.binding))
            ++audio_end;
        const auto audio_begin = batch[first]->time.utc().time_since_epoch().count();
        const auto until = audio_end == last ? end_us : batch[audio_end]->time.utc().time_since_epoch().count();
        using Pcm = domain::ArchiveSound;
        if (!sound.pcm.empty() && start < audio_begin) {
            const auto available = static_cast<std::int64_t>(sound.pcm.size()) / Pcm::bytes_per_sample;
            const auto skip = std::min<std::int64_t>(
                (audio_begin - start + Pcm::sample_period_us - 1) / Pcm::sample_period_us, available);
            sound.pcm.erase(0, static_cast<std::size_t>(skip * Pcm::bytes_per_sample));
            start += skip * Pcm::sample_period_us;
        }
        // Bytes of whole samples from the start of the sound up to the moment.
        const auto bytes_until = [&](std::int64_t moment) {
            return static_cast<std::size_t>(std::max<std::int64_t>(0, moment - start) / Pcm::sample_period_us *
                                            Pcm::bytes_per_sample);
        };
        if (!sound.pcm.empty())
            sound.pcm.resize(std::min(sound.pcm.size(), bytes_until(until)));
        if (!sound.pcm.empty() && start >= begin_us && start < end_us)
            sound.pcm.resize(std::min(sound.pcm.size(), bytes_until(end_us)));
        else
            sound.pcm.clear();
        volume_.publish({origin_, batch.front()->time, end,
                         batch.front()->codec == ingest::VideoCodec::h265 ? "h265" : "h264"},
                        [&](const fs::path& path) { mux(path, batch, times); }, {}, payload_bytes,
                        {start, sound.pcm});
        // From the reception of the last frame of the block to its durable publication.
        observability::Registry::process()
            .histogram({.name = "bacuri_archive_publication_lag_seconds",
                        .help = "Time from receiving the last frame of a block to its durable publication."},
                       observability::archive_buckets)
            .observe(std::chrono::system_clock::now() - batch.back()->time.received_at());
    } catch (const std::exception& error) {
        if (storage_gap_) {
            storage_gap_->begin = std::min(storage_gap_->begin, begin_us);
            storage_gap_->end = std::max(storage_gap_->end, end_us);
        } else {
            storage_gap_ = Gap{origin_.point, origin_.binding, begin_us, end_us,
                               std::string{domain::name(domain::GapReason::storage_failure)}};
        }
        try {
            volume_.report_gap(*storage_gap_);
            storage_gap_.reset();
        } catch (const std::exception&) {
            std::fputs("Archive gap journal unavailable\n", stderr);
        }
        std::fprintf(stderr, "archive storage_failure point=%s begin_us=%lld end_us=%lld: %s\n",
                     origin_.point.c_str(),
                     static_cast<long long>(batch.front()->time.utc().time_since_epoch().count()),
                     static_cast<long long>(end.time_since_epoch().count()), error.what());
    }
}

void Writer::run(const std::stop_token& stop)
{
    using Clock = std::chrono::steady_clock;
    auto last = Clock::now();
    for (;;) {
        auto current = volume_.recording_rule(origin_.point);
        if (current != rule_) {
            if (rule_ && (!current || current->id != rule_->id || current->revision != rule_->revision ||
                          current->since != rule_->since)) {
                auto previous = std::make_shared<CaptureRule>(*rule_);
                previous->until = current && current->since > rule_->since
                                      ? current->since
                                      : std::chrono::duration_cast<domain::Microseconds>(
                                            std::chrono::system_clock::now().time_since_epoch())
                                            .count();
                rule_ = std::move(previous);
                finish();
                drain(true);
            } else if (!rule_) {
                frames_.clear();
                bytes_ = 0;
            }
            rule_ = std::move(current);
            if (audio_)
                audio_->policy_record(rule_ && rule_->audio == RecordingAudio::enabled);
        }
        // The wait returns as soon as a frame arrives. Its deadline bounds policy and stop checks.
        auto frame = queue_->wait_pop(Clock::now() + policy_check_interval);
        if (!frame) {
            if (stop.stop_requested()) {
                break;
            }
            if (Clock::now() - last >= idle_block_timeout) {
                finish();
                drain(false);
            }
            continue;
        }
        last = Clock::now();
        if (!frames_.empty()) {
            const auto& first = frames_.front()->time;
            if (frame->keyframe || frame->codec != frames_.front()->codec) {
                finish();
            } else if (frames_.size() >= max_block_frames ||
                       bytes_ + frame->access_unit.size() > max_block_bytes ||
                       frame->time.received_at() - first.received_at() > max_block_duration) {
                finish();
                discarded_ = Gap{origin_.point, origin_.binding, frame->time.utc().time_since_epoch().count(), 0,
                                 std::string{domain::name(domain::GapReason::keyframe_interval)}};
                std::fprintf(stderr, "archive keyframe interval exceeded point=%s; discarding until keyframe\n",
                             origin_.point.c_str());
            }
        }
        if (frames_.empty() && !frame->keyframe) {
            if (discarded_)
                discarded_->end = frame->time.utc().time_since_epoch().count() + 1;
            continue;
        }
        if (discarded_) {
            discarded_->end = frame->time.utc().time_since_epoch().count();
            report_discarded();
        }
        bytes_ += frame->access_unit.size();
        frames_.push_back(std::move(frame));
    }
    finish();
    drain(true);
    report_discarded();
    if (audio_)
        audio_->policy_record(false);
}

void Writer::report_discarded()
{
    if (!discarded_)
        return;
    const auto gap = std::move(*discarded_);
    discarded_.reset();
    // A discarded interval is a gap only where the policy expected a recording.
    if (gap.end <= gap.begin || !rule_ || !rule_->selects(gap.begin, origin_.binding))
        return;
    try {
        volume_.report_gap(gap);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "archive gap journal unavailable point=%s: %s\n", origin_.point.c_str(),
                     error.what());
    }
}
} // namespace bacuri::vms::archive
