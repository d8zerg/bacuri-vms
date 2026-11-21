#include "recorder/rtsp_session.hpp"
#include "recorder/frame_clock.hpp"

#include <gst/app/gstappsink.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtsp/gstrtspmessage.h>
#include <glib.h>
#include <gio/gio.h>

#include <algorithm>
#include <atomic>
#include <chrono>

namespace bacuri::vms::ingest
{
namespace
{
using namespace std::chrono_literals;
using namespace domain;

template <auto Release> struct ReleaseGst {
    template <typename T> void operator()(T* value) const
    {
        Release(value);
    }
};
template <typename T, auto Release> using Handle = std::unique_ptr<T, ReleaseGst<Release>>;

struct PacketStamp {
    GstClockTime pts{GST_CLOCK_TIME_NONE};
    std::optional<std::uint32_t> rtp;
    std::optional<std::uint64_t> ntp;
};

class PacketTimes
{
  public:
    void observe(GstBuffer* buffer)
    {
        if (!GST_BUFFER_PTS_IS_VALID(buffer)) {
            return;
        }
        GstRTPBuffer rtp{};
        if (!gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp)) {
            return;
        }
        PacketStamp stamp{GST_BUFFER_PTS(buffer), gst_rtp_buffer_get_timestamp(&rtp), std::nullopt};
        gst_rtp_buffer_unmap(&rtp);
        auto* meta = gst_buffer_get_reference_timestamp_meta(buffer, ntp_caps_.get());
        if (meta && GST_CLOCK_TIME_IS_VALID(meta->timestamp)) {
            stamp.ntp = meta->timestamp;
        }
        std::scoped_lock lock{mutex_};
        if (entries_.empty() || entries_.back().pts != stamp.pts) {
            if (entries_.size() == 32) {
                entries_.pop_front();
            }
            entries_.push_back(stamp);
        } else if (stamp.ntp) {
            entries_.back().ntp = stamp.ntp;
        }
    }

    PacketStamp lookup(GstClockTime pts)
    {
        std::scoped_lock lock{mutex_};
        const auto found = std::ranges::find(entries_, pts, &PacketStamp::pts);
        return found == entries_.end() ? PacketStamp{} : *found;
    }

  private:
    Handle<GstCaps, gst_caps_unref> ntp_caps_{gst_caps_new_empty_simple("timestamp/x-ntp")};
    std::mutex mutex_;
    std::deque<PacketStamp> entries_;
};

struct Selection {
    std::atomic<bool> selected{};
    std::atomic<bool> saw_other{};
};

gboolean select_h264(GstElement*, guint, GstCaps* caps, gpointer data)
{
    auto& selection = *static_cast<Selection*>(data);
    const auto* track = gst_caps_get_structure(caps, 0);
    const auto* encoding = gst_structure_get_string(track, "encoding-name");
    if (g_strcmp0(gst_structure_get_string(track, "media"), "video") == 0 && encoding &&
        g_ascii_strcasecmp(encoding, "H264") == 0) {
        return !selection.selected.exchange(true);
    }
    selection.saw_other = true;
    return FALSE;
}

GstPadProbeReturn observe_packet(GstPad*, GstPadProbeInfo* info, gpointer data)
{
    auto* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (buffer) {
        static_cast<PacketTimes*>(data)->observe(buffer);
    }
    return GST_PAD_PROBE_OK;
}

int port(GUri* uri)
{
    const int number = g_uri_get_port(uri);
    return number >= 0 ? number : (g_strcmp0(g_uri_get_scheme(uri), "rtsps") == 0 ? 322 : 554);
}

gboolean same_origin(GstElement* source, GstRTSPMessage* message, gpointer data)
{
    const auto original = static_cast<GUri*>(data);
    gchar* current_location{};
    g_object_get(source, "location", &current_location, nullptr);
    std::unique_ptr<gchar, decltype(&g_free)> location{current_location, g_free};
    Handle<GUri, g_uri_unref> current{location ? g_uri_parse(location.get(), G_URI_FLAGS_ENCODED, nullptr)
                                               : nullptr};
    if (!current || g_strcmp0(g_uri_get_host(current.get()), g_uri_get_host(original)) ||
        g_strcmp0(g_uri_get_scheme(current.get()), g_uri_get_scheme(original)) ||
        port(current.get()) != port(original))
        return FALSE;
    GstRTSPMethod method{};
    const gchar* target{};
    if (gst_rtsp_message_parse_request(message, &method, &target, nullptr) != GST_RTSP_OK) {
        return TRUE;
    }
    if (g_strcmp0(target, "*") == 0) {
        return TRUE;
    }
    Handle<GUri, g_uri_unref> uri{g_uri_parse(target, G_URI_FLAGS_ENCODED, nullptr)};
    return uri && !g_uri_get_userinfo(uri.get()) && g_uri_get_host(uri.get()) &&
           g_ascii_strcasecmp(g_uri_get_host(original), g_uri_get_host(uri.get())) == 0 &&
           (g_strcmp0(g_uri_get_scheme(original), g_uri_get_scheme(uri.get())) == 0 ||
            (g_strcmp0(g_uri_get_scheme(original), "rtsps") == 0 &&
             g_strcmp0(g_uri_get_scheme(uri.get()), "rtsp") == 0)) &&
           port(original) == port(uri.get());
}

struct StopPipeline {
    void operator()(GstElement* pipeline) const
    {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    }
};

} // namespace

std::string receive_rtsp(const Endpoint& endpoint, std::stop_token stop, const Receive& receive)
{
    gst_init(nullptr, nullptr);
    PacketTimes packets;
    FrameClock clock;
    Selection selection;
    std::atomic<bool> certificate_rejected{};
    Handle<GUri, g_uri_unref> origin{g_uri_parse(endpoint.url.c_str(), G_URI_FLAGS_ENCODED, nullptr)};
    if (!origin || !g_uri_get_host(origin.get())) {
        return "invalid_address";
    }
    GError* error{};
    std::unique_ptr<GstElement, StopPipeline> pipeline{
        gst_parse_launch("rtspsrc name=source protocols=tcp latency=250 drop-on-latency=true tcp-timeout=3000000 "
                         "teardown-timeout=100000000 add-reference-timestamp-meta=true "
                         "! application/x-rtp,media=video,encoding-name=H264 "
                         "! rtph264depay name=depay request-keyframe=true wait-for-keyframe=true "
                         "! h264parse config-interval=-1 ! video/x-h264,stream-format=byte-stream,alignment=au "
                         "! appsink name=frames sync=false max-buffers=4 max-bytes=8388608 wait-on-eos=false "
                         "enable-last-sample=false",
                         &error)};
    Handle<GError, g_error_free> failure{error};
    if (!pipeline || failure) {
        return "pipeline_error";
    }
    Handle<GstElement, gst_object_unref> source{gst_bin_get_by_name(GST_BIN(pipeline.get()), "source")};
    Handle<GstElement, gst_object_unref> sink{gst_bin_get_by_name(GST_BIN(pipeline.get()), "frames")};
    Handle<GstElement, gst_object_unref> depay{gst_bin_get_by_name(GST_BIN(pipeline.get()), "depay")};
    Handle<GstPad, gst_object_unref> pad{gst_element_get_static_pad(depay.get(), "sink")};
    Handle<GstBus, gst_object_unref> bus{gst_element_get_bus(pipeline.get())};
    gst_pad_add_probe(pad.get(), GST_PAD_PROBE_TYPE_BUFFER, observe_packet, &packets, nullptr);
    g_signal_connect(source.get(), "accept-certificate",
                     G_CALLBACK(+[](GstElement*, GTlsConnection*, GTlsCertificate*, GTlsCertificateFlags,
                                    gpointer data) -> gboolean {
                         static_cast<std::atomic<bool>*>(data)->store(true);
                         return FALSE;
                     }),
                     &certificate_rejected);
    g_signal_connect(source.get(), "select-stream", G_CALLBACK(select_h264), &selection);
    g_signal_connect(source.get(), "before-send", G_CALLBACK(same_origin), origin.get());
    g_object_set(source.get(), "location", endpoint.url.c_str(), "user-id", endpoint.username.c_str(), "user-pw",
                 endpoint.password.c_str(), nullptr);
    const auto* trust = g_getenv("BACURI_CAMERA_CA_FILE");
    if (endpoint.url.starts_with("rtsps://") && trust && *trust) {
        Handle<GTlsDatabase, g_object_unref> ca{g_tls_file_database_new(trust, nullptr)};
        if (!ca)
            return "certificate_error";
        g_object_set(source.get(), "tls-database", ca.get(), nullptr);
    }
    if (gst_element_set_state(pipeline.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        return certificate_rejected ? "certificate_error" : "pipeline_error";
    }
    using Steady = std::chrono::steady_clock;
    auto deadline = Steady::now() + 8s;
    while (!stop.stop_requested()) {
        Handle<GstSample, gst_sample_unref> sample{
            gst_app_sink_try_pull_sample(GST_APP_SINK(sink.get()), 100 * GST_MSECOND)};
        // Drain the entire bus, including normal state changes and streaming notifications.
        while (Handle<GstMessage, gst_message_unref> message{gst_bus_pop(bus.get())}) {
            if (certificate_rejected)
                return "certificate_error";
            if (selection.saw_other && !selection.selected &&
                GST_MESSAGE_TYPE(message.get()) == GST_MESSAGE_ERROR) {
                return "incompatible_profile";
            }
            if (GST_MESSAGE_TYPE(message.get()) == GST_MESSAGE_ERROR ||
                GST_MESSAGE_TYPE(message.get()) == GST_MESSAGE_EOS) {
                return "source_unavailable";
            }
        }
        if (!sample) {
            if (Steady::now() >= deadline) {
                return selection.saw_other && !selection.selected ? "incompatible_profile" : "frame_timeout";
            }
            continue;
        }
        auto* buffer = gst_sample_get_buffer(sample.get());
        auto* caps = gst_sample_get_caps(sample.get());
        VideoFormat format{};
        if (!buffer || !caps || gst_caps_is_empty(caps)) {
            return "incompatible_profile";
        }
        const auto* type = gst_caps_get_structure(caps, 0);
        if (!gst_structure_get_int(type, "width", &format.width) ||
            !gst_structure_get_int(type, "height", &format.height) || format.width <= 0 || format.height <= 0) {
            return "incompatible_profile";
        }
        const auto size = gst_buffer_get_size(buffer);
        if (size == 0 || size > limits(Profile::main).bytes ||
            GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_CORRUPTED)) {
            return "invalid_access_unit";
        }
        const auto pts = GST_BUFFER_PTS(buffer);
        const auto stamp = packets.lookup(pts);
        const auto received = std::chrono::time_point_cast<Microseconds>(std::chrono::system_clock::now());
        const auto pts_ns = GST_CLOCK_TIME_IS_VALID(pts) ? std::optional{pts} : std::nullopt;
        auto frame = std::make_shared<Frame>(Frame{
            std::vector<std::byte>(size), clock.project(received, {stamp.ntp, pts_ns}), stamp.rtp, stamp.ntp,
            pts_ns, !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT),
            GST_BUFFER_DURATION_IS_VALID(buffer) ? std::optional{GST_BUFFER_DURATION(buffer)} : std::nullopt});
        if (gst_buffer_extract(buffer, 0, frame->h264.data(), size) != size) {
            return "invalid_access_unit";
        }
        receive(std::move(frame), format, GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT));
        deadline = Steady::now() + 5s;
    }
    return "stopped";
}
} // namespace bacuri::vms::ingest
