#include "media/webrtc.hpp"
#include "recorder/ingest.hpp"
#include <charconv>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <utility>

namespace bacuri::vms::media
{
namespace
{
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using Description = GstHandle<GstWebRTCSessionDescription, gst_webrtc_session_description_free>;

class Promise
{
  public:
    Promise()
    {
        auto notify = std::make_unique<std::promise<void>>();
        ready_ = notify->get_future();
        promise_.reset(gst_promise_new_with_change_func(
            [](GstPromise*, gpointer data) { static_cast<std::promise<void>*>(data)->set_value(); },
            notify.release(), [](gpointer data) { delete static_cast<std::promise<void>*>(data); }));
    }
    GstPromise* get() const
    {
        return promise_.get();
    }
    const GstStructure* reply(Clock::time_point deadline)
    {
        if (ready_.wait_until(deadline) != std::future_status::ready) {
            gst_promise_interrupt(get());
            throw std::runtime_error("SDP negotiation timed out");
        }
        if (gst_promise_wait(get()) != GST_PROMISE_RESULT_REPLIED) {
            throw std::runtime_error("SDP negotiation interrupted");
        }
        const auto* data = gst_promise_get_reply(get());
        if (data && gst_structure_has_field(data, "error")) {
            throw std::invalid_argument("Unsupported SDP");
        }
        return data;
    }

  private:
    std::future<void> ready_;
    GstHandle<GstPromise, gst_promise_unref> promise_;
};

Description parse(const std::string& sdp)
{
    if (sdp.empty() || sdp.size() > 65536 || sdp.contains('\0')) {
        throw std::invalid_argument("Invalid SDP size");
    }
    GstSDPMessage* parsed = nullptr;
    auto status = gst_sdp_message_new_from_text(sdp.c_str(), &parsed);
    GstHandle<GstSDPMessage, gst_sdp_message_free> message{parsed};
    if (status != GST_SDP_OK || !message || gst_sdp_message_medias_len(parsed) != 1) {
        throw std::invalid_argument("Expected one video track");
    }
    const auto* track = gst_sdp_message_get_media(parsed, 0);
    if (g_strcmp0(gst_sdp_media_get_media(track), "video") != 0 ||
        g_strcmp0(gst_sdp_media_get_proto(track), "UDP/TLS/RTP/SAVPF") != 0 || !gst_sdp_media_get_port(track) ||
        !gst_sdp_media_get_attribute_val(track, "recvonly") ||
        !gst_sdp_media_get_attribute_val(track, "candidate")) {
        throw std::invalid_argument("Expected complete receive-only video offer");
    }
    return Description{gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, message.release())};
}
int video_payload(const GstSDPMedia* track, bool hevc)
{
    for (guint i = 0; i < gst_sdp_media_formats_len(track); ++i) {
        std::string_view fmt{gst_sdp_media_get_format(track, i)};
        int pt{};
        const auto [end, error] = std::from_chars(fmt.data(), fmt.data() + fmt.size(), pt);
        if (error != std::errc{} || end != fmt.data() + fmt.size() || pt < 96 || pt > 127) {
            continue;
        }
        GstHandle<GstCaps, gst_caps_unref> caps{gst_sdp_media_get_caps_from_media(track, pt)};
        if (!caps || gst_caps_is_empty(caps.get())) {
            continue;
        }
        const auto* fields = gst_caps_get_structure(caps.get(), 0);
        if (hevc) {
            if (g_strcmp0(gst_structure_get_string(fields, "encoding-name"), "H265") == 0)
                return pt;
            continue;
        }
        const auto* profile = gst_structure_get_string(fields, "profile-level-id");
        if (g_strcmp0(gst_structure_get_string(fields, "encoding-name"), "H264") == 0 &&
            g_strcmp0(gst_structure_get_string(fields, "packetization-mode"), "1") == 0 &&
            (!profile || std::string_view{profile}.starts_with("42"))) {
            return pt;
        }
    }
    throw std::invalid_argument("No compatible video codec in the SDP offer");
}
} // namespace

struct PeerEvents {
    std::mutex mutex;
    std::condition_variable_any changed;
    std::uint64_t generation{}, observed{};

    void signal() noexcept
    {
        {
            std::scoped_lock guard{mutex};
            ++generation;
        }
        changed.notify_all();
    }
    static void on_notify(GObject*, GParamSpec*, gpointer data)
    {
        static_cast<PeerEvents*>(data)->signal();
    }
    static GstBusSyncReply on_message(GstBus*, GstMessage* message, gpointer data)
    {
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
            static_cast<PeerEvents*>(data)->signal();
        return GST_BUS_PASS;
    }
};

WebRtcPeer::WebRtcPeer(const std::string& offer, LiveFormat format, std::string_view regions)
    : events_{std::make_unique<PeerEvents>()}, mask_{regions}, offer_{parse(offer)}
{
    const auto [source, output] = format;
    const bool hevc = output == "h265";
    auto pt = std::to_string(video_payload(gst_sdp_message_get_media(offer_->sdp, 0), hevc));
    std::string launch = "appsrc name=input is-live=true do-timestamp=true format=time caps=";
    launch += source == "h265" ? "video/x-h265,stream-format=byte-stream,alignment=au ! h265parse "
                               : "video/x-h264,stream-format=byte-stream,alignment=au ! h264parse ";
    if (source != output || !mask_.empty()) {
        if (output != "h264")
            throw std::invalid_argument("Unsupported video transform");
        launch += source == "h265" ? "! avdec_h265 " : "! avdec_h264 ";
        launch +=
            "max-threads=2 ! videoconvert ! video/x-raw,format=I420 ! x264enc name=encoder bitrate=2000 threads=2 "
            "tune=zerolatency speed-preset=ultrafast key-int-max=25 ! video/x-h264,profile=baseline ! h264parse ";
    }
    launch += hevc ? "! rtph265pay " : "! rtph264pay ";
    launch += "config-interval=-1 aggregate-mode=zero-latency pt=" + pt +
              " ! application/x-rtp,media=video,clock-rate=90000,";
    launch += hevc ? "encoding-name=H265," : "encoding-name=H264,packetization-mode=(string)1,";
    launch += "payload=(int)" + pt + " ! webrtcbin name=peer bundle-policy=max-bundle";
    GError* raw_error = nullptr;
    pipeline_.reset(gst_parse_launch(launch.c_str(), &raw_error));
    GstHandle<GError, g_error_free> error{raw_error};
    if (!pipeline_ || error) {
        throw std::runtime_error("WebRTC pipeline unavailable");
    }
    if (!mask_.empty()) {
        GstHandle<GstElement, gst_object_unref> encoder{gst_bin_get_by_name(GST_BIN(pipeline_.get()), "encoder")};
        mask_.attach(encoder.get());
    }
    rtc_.reset(gst_bin_get_by_name(GST_BIN(pipeline_.get()), "peer"));
    input_.reset(gst_bin_get_by_name(GST_BIN(pipeline_.get()), "input"));
    g_signal_connect(rtc_.get(), "notify::connection-state", G_CALLBACK(PeerEvents::on_notify), events_.get());
    g_signal_connect(rtc_.get(), "notify::ice-gathering-state", G_CALLBACK(PeerEvents::on_notify), events_.get());
    {
        GstHandle<GstBus, gst_object_unref> bus{gst_element_get_bus(pipeline_.get())};
        gst_bus_set_sync_handler(bus.get(), PeerEvents::on_message, events_.get(), nullptr);
    }
    // The local deployment uses direct UDP candidates on the media bridge.
    GObject* ice_raw = nullptr;
    g_object_get(rtc_.get(), "ice-agent", &ice_raw, nullptr);
    GstHandle<GObject, g_object_unref> ice{ice_raw};
    GObject* agent_raw = nullptr;
    if (ice) {
        g_object_get(ice.get(), "agent", &agent_raw, nullptr);
    }
    GstHandle<GObject, g_object_unref> agent{agent_raw};
    if (!agent) {
        throw std::runtime_error("ICE agent unavailable");
    }
    g_object_set(ice.get(), "ice-tcp", FALSE, nullptr);
    g_object_set(agent.get(), "upnp", FALSE, nullptr);
    const auto limit = ingest::limits(ingest::Profile::main);
    gst_app_src_set_max_bytes(GST_APP_SRC(input_.get()), limit.bytes);
    gst_app_src_set_max_buffers(GST_APP_SRC(input_.get()), limit.frames);
    GstHandle<GstPad, gst_object_unref> pad{gst_element_get_static_pad(rtc_.get(), "sink_0")};
    GstWebRTCRTPTransceiver* raw = nullptr;
    if (pad) {
        g_object_get(pad.get(), "transceiver", &raw, nullptr);
    }
    GstHandle<GstWebRTCRTPTransceiver, gst_object_unref> transceiver{raw};
    if (!transceiver) {
        throw std::runtime_error("Cannot allocate video transceiver");
    }
    g_object_set(raw, "direction", GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY, nullptr);
}
WebRtcPeer::~WebRtcPeer()
{
    stop();
    if (rtc_)
        g_signal_handlers_disconnect_by_data(rtc_.get(), events_.get());
    if (pipeline_) {
        GstHandle<GstBus, gst_object_unref> bus{gst_element_get_bus(pipeline_.get())};
        gst_bus_set_sync_handler(bus.get(), nullptr, nullptr, nullptr);
    }
}

void WebRtcPeer::wait(std::stop_token stop, Clock::time_point deadline)
{
    std::unique_lock lock{events_->mutex};
    events_->changed.wait_until(lock, std::move(stop), deadline,
                                [&] { return events_->generation != events_->observed; });
    events_->observed = events_->generation;
}

void WebRtcPeer::notify() noexcept
{
    events_->signal();
}
void WebRtcPeer::stop() noexcept
{
    gst_element_set_state(pipeline_.get(), GST_STATE_NULL);
}

std::string WebRtcPeer::answer(Clock::time_point deadline)
{
    if (gst_element_set_state(pipeline_.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        throw std::runtime_error("Cannot start WebRTC transport");
    }
    Promise remote;
    g_signal_emit_by_name(rtc_.get(), "set-remote-description", offer_.get(), remote.get());
    remote.reply(deadline);
    Promise create;
    g_signal_emit_by_name(rtc_.get(), "create-answer", nullptr, create.get());
    auto* reply = create.reply(deadline);
    GstWebRTCSessionDescription* raw = nullptr;
    if (!reply || !gst_structure_get(reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &raw, nullptr)) {
        throw std::runtime_error("Cannot create SDP answer");
    }
    Description answer{raw};
    const auto* track = gst_sdp_message_get_media(answer->sdp, 0);
    if (!track || !gst_sdp_media_get_port(track) || !gst_sdp_media_get_attribute_val(track, "sendonly")) {
        throw std::invalid_argument("Video track rejected");
    }
    Promise local;
    g_signal_emit_by_name(rtc_.get(), "set-local-description", answer.get(), local.get());
    local.reply(deadline);
    while (Clock::now() < deadline) {
        GstWebRTCICEGatheringState state{};
        g_object_get(rtc_.get(), "ice-gathering-state", &state, nullptr);
        if (state == GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE) {
            raw = nullptr;
            g_object_get(rtc_.get(), "local-description", &raw, nullptr);
            Description complete{raw};
            if (!complete ||
                !gst_sdp_media_get_attribute_val(gst_sdp_message_get_media(complete->sdp, 0), "candidate")) {
                break;
            }
            GstHandle<gchar, g_free> text{gst_sdp_message_as_text(complete->sdp)};
            if (text && std::string_view{text.get()}.size() <= 65536) {
                return text.get();
            }
            break;
        }
        wait({}, deadline);
    }
    throw std::runtime_error("ICE gathering unavailable");
}

GstWebRTCPeerConnectionState WebRtcPeer::state() const
{
    GstHandle<GstBus, gst_object_unref> bus{gst_element_get_bus(pipeline_.get())};
    GstHandle<GstMessage, gst_message_unref> failure{gst_bus_pop_filtered(bus.get(), GST_MESSAGE_ERROR)};
    if (failure)
        return GST_WEBRTC_PEER_CONNECTION_STATE_FAILED;
    GstWebRTCPeerConnectionState value{};
    g_object_get(rtc_.get(), "connection-state", &value, nullptr);
    return value;
}

void WebRtcPeer::push(std::string_view bytes, bool keyframe)
{
    auto* input = GST_APP_SRC(input_.get());
    const auto limit = ingest::limits(ingest::Profile::main);
    if (bytes.size() > limit.bytes || gst_app_src_get_current_level_buffers(input) >= limit.frames ||
        gst_app_src_get_current_level_bytes(input) > limit.bytes - bytes.size()) {
        need_keyframe_ = true;
        return;
    }
    if (need_keyframe_ && !keyframe) {
        return;
    }
    need_keyframe_ = false;
    auto* buffer = gst_buffer_new_allocate(nullptr, bytes.size(), nullptr);
    if (!buffer) {
        throw std::bad_alloc{};
    }
    gst_buffer_fill(buffer, 0, bytes.data(), bytes.size());
    if (!keyframe) {
        GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    }
    if (gst_app_src_push_buffer(input, buffer) != GST_FLOW_OK) {
        throw std::runtime_error("Live video transport closed");
    }
}
} // namespace bacuri::vms::media
