#include "media/clip.hpp"
#include "archive/evidence.hpp"
#include "media/privacy.hpp"
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <chrono>
#include <deque>
#include <mutex>
#include <stdexcept>

namespace bacuri::vms::archive
{
namespace
{
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
template <class T> auto own(T* p)
{
    return std::unique_ptr<T, decltype(&gst_object_unref)>{p, gst_object_unref};
}
// x264enc and h264parse can replace an input duration with the nominal frame period.
struct FrameTiming {
    std::mutex mutex;
    std::deque<std::pair<GstClockTime, GstClockTime>> samples;
    GstClockTime base = GST_CLOCK_TIME_NONE;
    bool invalid{};
    static GstPadProbeReturn restore(GstPad*, GstPadProbeInfo* info, gpointer data)
    {
        auto& timing = *static_cast<FrameTiming*>(data);
        std::lock_guard lock{timing.mutex};
        auto* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        if (timing.samples.empty() || !GST_BUFFER_PTS_IS_VALID(buffer)) {
            timing.invalid = true;
            return GST_PAD_PROBE_DROP;
        }
        if (timing.base == GST_CLOCK_TIME_NONE)
            timing.base = GST_BUFFER_PTS(buffer);
        const auto [pts, duration] = timing.samples.front();
        timing.samples.pop_front();
        buffer = gst_buffer_make_writable(buffer);
        GST_PAD_PROBE_INFO_DATA(info) = buffer;
        GST_BUFFER_PTS(buffer) = GST_BUFFER_DTS(buffer) = timing.base + pts;
        GST_BUFFER_DURATION(buffer) = duration;
        return GST_PAD_PROBE_OK;
    }
};
struct Pipeline {
    decltype(own(static_cast<GstElement*>(nullptr))) pipeline;
    explicit Pipeline(const char* spec) : pipeline{own(gst_parse_launch(spec, nullptr))}
    {
        if (!pipeline)
            throw std::runtime_error("Cannot create export pipeline");
    }
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    ~Pipeline()
    {
        gst_element_set_state(pipeline.get(), GST_STATE_NULL);
    }
    auto element(const char* name)
    {
        auto e = own(gst_bin_get_by_name(GST_BIN(pipeline.get()), name));
        if (!e)
            throw std::runtime_error("Missing export pipeline element");
        return e;
    }
    void start()
    {
        if (gst_element_set_state(pipeline.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
            throw std::runtime_error("Cannot start export pipeline");
    }
    bool finished(GstClockTime timeout)
    {
        auto bus = own(gst_element_get_bus(pipeline.get()));
        std::unique_ptr<GstMessage, decltype(&gst_message_unref)> msg{
            gst_bus_timed_pop_filtered(bus.get(), timeout,
                                       // GStreamer accepts a bit mask of message types.
                                       // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
                                       static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR)),
            gst_message_unref};
        if (msg && GST_MESSAGE_TYPE(msg.get()) == GST_MESSAGE_ERROR)
            throw std::runtime_error("Export codec failed");
        return bool(msg);
    }
};
} // namespace
ClipFrames select_frames(const std::filesystem::path& input, const std::filesystem::path& output,
                         std::int64_t from_us, std::int64_t until_us, bool fragmented, std::string_view regions)
{
    if (from_us < 0 || until_us <= from_us)
        throw std::invalid_argument("Invalid export interval");
    gst_init(nullptr, nullptr);
    Pipeline decoder{"filesrc name=file ! qtdemux ! decodebin ! videoconvert ! "
                     "video/x-raw,format=I420 ! appsink name=frames max-buffers=2 sync=false"};
    FrameTiming timing;
    media::PrivacyMask mask{regions};
    std::string encoding =
        "appsrc name=frames format=time ! x264enc name=encoder threads=2 tune=zerolatency speed-preset=ultrafast "
        "pass=quant quantizer=16 "
        "! video/x-h264,profile=baseline ! h264parse name=parser ! mp4mux trak-timescale=1000000 "
        "movie-timescale=1000000 ";
    if (fragmented)
        encoding += "fragment-duration=500 ";
    encoding += "! filesink name=file";
    Pipeline encoder{encoding.c_str()};
    mask.attach(encoder.element("encoder").get());
    auto decoded = decoder.element("frames"), raw = encoder.element("frames");
    g_object_set(decoder.element("file").get(), "location", input.c_str(), nullptr);
    g_object_set(encoder.element("file").get(), "location", output.c_str(), nullptr);
    auto pad = own(gst_element_get_static_pad(encoder.element("parser").get(), "src"));
    gst_pad_add_probe(pad.get(), GST_PAD_PROBE_TYPE_BUFFER, FrameTiming::restore, &timing, nullptr);
    decoder.start();
    ClipFrames result{};
    GstClockTime first = GST_CLOCK_TIME_NONE, previous = GST_CLOCK_TIME_NONE;
    const auto deadline = Clock::now() + 30s;
    for (;;) {
        if (Clock::now() >= deadline)
            throw std::runtime_error("Export decoding timed out");
        std::unique_ptr<GstSample, decltype(&gst_sample_unref)> sample{
            gst_app_sink_try_pull_sample(GST_APP_SINK(decoded.get()), GST_SECOND / 4), gst_sample_unref};
        if (!sample) {
            decoder.finished(0);
            if (gst_app_sink_is_eos(GST_APP_SINK(decoded.get())))
                break;
            continue;
        }
        auto* buffer = gst_sample_get_buffer(sample.get());
        auto t = GST_BUFFER_PTS(buffer);
        if (!GST_CLOCK_TIME_IS_VALID(t) || (previous != GST_CLOCK_TIME_NONE && t <= previous))
            throw std::runtime_error("Invalid presentation timestamp");
        previous = t;
        if (first == GST_CLOCK_TIME_NONE)
            first = t;
        const auto pos = static_cast<std::int64_t>((t - first) / GST_USECOND);
        if (pos >= until_us)
            break;
        if (pos < from_us)
            continue;
        if (!result.count) {
            result.begin_us = pos;
            gst_app_src_set_caps(GST_APP_SRC(raw.get()), gst_sample_get_caps(sample.get()));
            encoder.start();
        }
        auto duration = GST_BUFFER_DURATION(buffer);
        const auto limit = static_cast<GstClockTime>(until_us - pos) * GST_USECOND;
        duration = GST_CLOCK_TIME_IS_VALID(duration) ? std::min(duration, limit)
                                                     : std::min<GstClockTime>(GST_USECOND, limit);
        auto* frame = gst_buffer_copy(buffer);
        GST_BUFFER_PTS(frame) = static_cast<GstClockTime>(pos - result.begin_us) * GST_USECOND;
        GST_BUFFER_DTS(frame) = GST_CLOCK_TIME_NONE;
        GST_BUFFER_DURATION(frame) = duration;
        {
            std::lock_guard lock{timing.mutex};
            timing.samples.emplace_back(GST_BUFFER_PTS(frame), duration);
        }
        if (gst_app_src_push_buffer(GST_APP_SRC(raw.get()), frame) != GST_FLOW_OK)
            throw std::runtime_error("Cannot encode export frame");
        result.end_us = pos + std::max<std::int64_t>(1, static_cast<std::int64_t>(duration / GST_USECOND));
        ++result.count;
        if (result.count % 25 == 0 && std::filesystem::file_size(output) > evidence_limit)
            throw std::runtime_error("Export media exceeds size limit");
        // Bound queued raw frames even if the encoder stalls.
        while (gst_app_src_get_current_level_buffers(GST_APP_SRC(raw.get())) > 2) {
            if (encoder.finished(GST_SECOND / 100) || Clock::now() >= deadline)
                throw std::runtime_error("Export encoder stalled");
        }
    }
    if (result.count &&
        (gst_app_src_end_of_stream(GST_APP_SRC(raw.get())) != GST_FLOW_OK || !encoder.finished(10 * GST_SECOND)))
        throw std::runtime_error("Cannot finalize export clip");
    std::lock_guard lock{timing.mutex};
    if (timing.invalid || !timing.samples.empty())
        throw std::runtime_error("Export frame timing mismatch");
    return result;
}
} // namespace bacuri::vms::archive
