#include "archive/writer.hpp"
#include <gst/app/gstappsrc.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <utility>

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
    auto batch = std::exchange(frames_, {});
    const auto payload_bytes = std::exchange(bytes_, 0);
    const auto times = sample_times(batch);
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
        auto sound =
            audio_ ? audio_->recorded(received, domain::Microseconds{end_us - begin_us}) : ingest::Sound{};
        auto start = begin_us + (sound.start_us - received);
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
        auto frame = queue_->try_pop();
        if (!frame) {
            if (stop.stop_requested()) {
                break;
            }
            if (Clock::now() - last >= 5s) {
                finish();
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
}
} // namespace bacuri::vms::archive
