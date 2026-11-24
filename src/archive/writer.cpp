#include "archive/writer.hpp"
#include "recorder/replay.hpp"
#include <gst/app/gstappsrc.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <utility>
#include <thread>

namespace bacuri::vms::archive
{
namespace
{
using namespace std::chrono_literals;
using Frames = std::vector<ingest::FramePtr>;

struct StopPipeline {
    void operator()(GstElement* pipeline) const
    {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    }
};

std::uint64_t duration(const Frames& frames, std::size_t index)
{
    const auto& frame = *frames[index];
    if (auto value = frame.duration_ns; value && *value > 0 && *value <= GST_SECOND) {
        return *value;
    }
    if (frames.size() > 1) {
        const auto i = std::min(index, frames.size() - 2);
        const auto a = frames[i]->pts_ns, b = frames[i + 1]->pts_ns;
        if (a && b && *b > *a && *b - *a <= GST_SECOND) {
            return *b - *a;
        }
    }
    // Only the observed microsecond is known when a single frame has no duration.
    return GST_USECOND;
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
        const auto offset =
            base && pts && *pts >= *base && *pts - *base <= 15 * GST_SECOND ? *pts - *base : cursor;
        result.push_back({offset, duration(frames, i)});
        cursor = offset + result.back().duration;
    }
    return result;
}

void mux(const fs::path& path, const Frames& frames, const std::vector<SampleTime>& times)
{
    GError* error{};
    const std::string chain = frames.front()->codec == ingest::VideoCodec::h265
                                  ? "video/x-h265,stream-format=byte-stream,alignment=au ! h265parse ! "
                                    "video/x-h265,stream-format=hvc1,alignment=au ! h265timestamper"
                                  : "video/x-h264,stream-format=byte-stream,alignment=au ! h264parse ! "
                                    "video/x-h264,stream-format=avc,alignment=au ! h264timestamper";
    const auto launch = "appsrc name=input format=time caps=" + chain +
                        " ! mp4mux fragment-duration=500 trak-timescale=90000 ! filesink name=output sync=false";
    std::unique_ptr<GstElement, StopPipeline> pipeline{gst_parse_launch(launch.c_str(), &error)};
    std::unique_ptr<GError, decltype(&g_error_free)> failure{error, g_error_free};
    if (failure || !pipeline) {
        throw std::runtime_error("Archive muxer unavailable");
    }
    const auto element = [&](const char* name) {
        return std::unique_ptr<GstElement, decltype(&gst_object_unref)>{
            gst_bin_get_by_name(GST_BIN(pipeline.get()), name), gst_object_unref};
    };
    auto sink = element("output"), source = element("input");
    g_object_set(sink.get(), "location", path.c_str(), nullptr);
    if (gst_element_set_state(pipeline.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        throw std::runtime_error("Cannot start archive muxer");
    }

    for (std::size_t i = 0; i < frames.size(); ++i) {
        const auto& frame = *frames[i];
        auto* buffer = gst_buffer_new_allocate(nullptr, frame.access_unit.size(), nullptr);
        if (!buffer) {
            throw std::bad_alloc{};
        }
        gst_buffer_fill(buffer, 0, frame.access_unit.data(), frame.access_unit.size());
        GST_BUFFER_PTS(buffer) = times[i].pts;
        GST_BUFFER_DURATION(buffer) = times[i].duration;
        if (!frame.keyframe) {
            GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
        }
        if (gst_app_src_push_buffer(GST_APP_SRC(source.get()), buffer) != GST_FLOW_OK) {
            throw std::runtime_error("Archive muxer rejected access unit");
        }
    }
    if (gst_app_src_end_of_stream(GST_APP_SRC(source.get())) != GST_FLOW_OK) {
        throw std::runtime_error("Cannot finalize archive muxer");
    }
    std::unique_ptr<GstBus, decltype(&gst_object_unref)> bus{gst_element_get_bus(pipeline.get()),
                                                             gst_object_unref};
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + 5s;
    for (;;) {
        const auto remaining = std::chrono::ceil<std::chrono::nanoseconds>(deadline - Clock::now());
        if (remaining <= 0ns) {
            break;
        }
        std::unique_ptr<GstMessage, decltype(&gst_message_unref)> message{
            gst_bus_timed_pop(bus.get(), static_cast<GstClockTime>(remaining.count())), gst_message_unref};
        if (!message || GST_MESSAGE_TYPE(message.get()) == GST_MESSAGE_ERROR) {
            break;
        }
        if (GST_MESSAGE_TYPE(message.get()) == GST_MESSAGE_EOS) {
            return;
        }
    }
    throw std::runtime_error("Archive muxer failed to finalize media");
}
} // namespace
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
    const auto flush = [&] {
        if (frames.empty())
            return;
        auto times = sample_times(frames);
        const auto first = frames.front()->time.utc().time_since_epoch().count();
        const auto end = frames.back()->time.utc().time_since_epoch().count() +
                         std::max<std::int64_t>(1, static_cast<std::int64_t>(times.back().duration / GST_USECOND));
        if (first >= job.begin && end <= job.end && end > first &&
            volume.publish(
                {job.origin, frames.front()->time, UtcTime{Microseconds{end}},
                 frames.front()->codec == ingest::VideoCodec::h265 ? "h265" : "h264", job.recording},
                [&](const fs::path& path) { mux(path, frames, times); }, {}, bytes)) {
            ++result.blocks;
            result.duration_us += end - first;
        }
        frames.clear();
        bytes = 0;
    };
    const auto outcome = ingest::receive_replay(
        job.endpoint, {job.begin - job.offset, job.end - job.offset},
        [&] { return cancelled() || stop.stop_requested(); },
        [&](ingest::FramePtr incoming, ingest::VideoFormat, bool discontinuity) {
            if (cancelled() || std::chrono::steady_clock::now() - started > 30s) {
                stop.request_stop();
                return;
            }
            // Pace the replay reader at 4 Mbit/s. Socket backpressure bounds the decoder queue.
            eligible = std::max(eligible, std::chrono::steady_clock::now()) +
                       std::chrono::microseconds{2 * incoming->access_unit.size()};
            if (eligible > started + 30s) {
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
    if (result.outcome.empty())
        result.outcome = result.blocks ? "copied" : outcome == "frame_timeout" ? "timeout" : "no_frames";
    return result;
}

Writer::Writer(Volume& volume, Origin origin, std::shared_ptr<ingest::FrameQueue> queue,
               std::shared_ptr<ingest::CameraAudio> audio)
    : volume_{volume}, origin_{std::move(origin)}, queue_{std::move(queue)}, audio_{std::move(audio)},
      worker_{[this](std::stop_token stop) { run(stop); }}
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
        if (matching && !stopping && !rule_->continuous && (rule_->manual || rule_->event) &&
            age < std::chrono::seconds{(rule_->event ? rule_->pre : 0) + 3} &&
            buffered_bytes_ <= 128uz * 1024 * 1024)
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
        if (!sound.pcm.empty() && start < audio_begin) {
            const auto skip = std::min<std::int64_t>((audio_begin - start + 124) / 125,
                                                     static_cast<std::int64_t>(sound.pcm.size() / 2));
            sound.pcm.erase(0, skip * 2);
            start += skip * 125;
        }
        if (!sound.pcm.empty())
            sound.pcm.resize(
                std::min<std::size_t>(sound.pcm.size(), std::max<std::int64_t>(0, until - start) / 125 * 2));
        if (!sound.pcm.empty() && start >= begin_us && start < end_us)
            sound.pcm.resize(std::min<std::size_t>(sound.pcm.size(), (end_us - start) / 125 * 2));
        else
            sound.pcm.clear();
        volume_.publish({origin_, batch.front()->time, end,
                         batch.front()->codec == ingest::VideoCodec::h265 ? "h265" : "h264"},
                        [&](const fs::path& path) { mux(path, batch, times); }, {}, payload_bytes,
                        {start, sound.pcm});
    } catch (const std::exception& error) {
        if (storage_gap_) {
            storage_gap_->begin = std::min(storage_gap_->begin, begin_us);
            storage_gap_->end = std::max(storage_gap_->end, end_us);
        } else {
            storage_gap_ = Gap{origin_.point, origin_.binding, begin_us, end_us, "storage_failure"};
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

void Writer::run(std::stop_token stop)
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
        auto frame = queue_->try_pop();
        if (!frame) {
            if (stop.stop_requested()) {
                break;
            }
            if (Clock::now() - last >= 5s) {
                finish();
                drain(false);
            }
            std::this_thread::sleep_for(10ms);
            continue;
        }
        last = Clock::now();
        if (!frames_.empty()) {
            const auto& first = frames_.front()->time;
            if (frame->keyframe || frame->codec != frames_.front()->codec) {
                finish();
            } else if (frames_.size() >= 900 || bytes_ + frame->access_unit.size() > 32uz * 1024 * 1024 ||
                       frame->time.received_at() - first.received_at() > 15s) {
                finish();
                std::fprintf(stderr, "archive GOP limit reached point=%s; waiting for keyframe\n",
                             origin_.point.c_str());
            }
        }
        if (frames_.empty() && !frame->keyframe) {
            continue;
        }
        bytes_ += frame->access_unit.size();
        frames_.push_back(std::move(frame));
    }
    finish();
    drain(true);
    if (audio_)
        audio_->policy_record(false);
}
} // namespace bacuri::vms::archive
