#include <gst/app/gstappsink.h>
#include <gst/sdp/gstsdpmessage.h>
#include <gst/webrtc/webrtc.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
template <class T, auto Release> struct Deleter {
    void operator()(T* p) const
    {
        Release(p);
    }
};
template <class T, auto Release> using Handle = std::unique_ptr<T, Deleter<T, Release>>;
using Description = Handle<GstWebRTCSessionDescription, gst_webrtc_session_description_free>;
using Element = Handle<GstElement, gst_object_unref>;
using Promise = Handle<GstPromise, gst_promise_unref>;

void set_description(GstElement* rtc, const char* signal, GstWebRTCSessionDescription* description)
{
    Promise ack{gst_promise_new()};
    g_signal_emit_by_name(rtc, signal, description, ack.get());
    if (gst_promise_wait(ack.get()) != GST_PROMISE_RESULT_REPLIED)
        throw std::runtime_error("SDP update failed");
    const auto* reply = gst_promise_get_reply(ack.get());
    if (reply && gst_structure_has_field(reply, "error"))
        throw std::runtime_error("SDP rejected");
}

void exchange(GstElement* rtc)
{
    using namespace std::chrono_literals;
    Promise created{gst_promise_new()};
    g_signal_emit_by_name(rtc, "create-offer", nullptr, created.get());
    GstWebRTCSessionDescription* raw{};
    if (gst_promise_wait(created.get()) != GST_PROMISE_RESULT_REPLIED ||
        !gst_structure_get(gst_promise_get_reply(created.get()), "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION,
                           &raw, nullptr))
        throw std::runtime_error("HEVC offer unavailable");
    Description offer{raw};
    set_description(rtc, "set-local-description", offer.get());
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    GstWebRTCICEGatheringState state{};
    while (state != GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("ICE gathering timeout");
        g_object_get(rtc, "ice-gathering-state", &state, nullptr);
        std::this_thread::sleep_for(10ms);
    }
    raw = nullptr;
    g_object_get(rtc, "local-description", &raw, nullptr);
    Description local{raw};
    Handle<gchar, g_free> text{gst_sdp_message_as_text(local->sdp)};
    Handle<gchar, g_free> encoded{
        g_base64_encode(reinterpret_cast<const guchar*>(text.get()), std::char_traits<char>::length(text.get()))};
    std::cout << encoded.get() << std::endl;
    std::string line;
    if (!std::getline(std::cin, line) || line.size() > 131072)
        throw std::runtime_error("Missing SDP answer");
    gsize size{};
    Handle<guchar, g_free> bytes{g_base64_decode(line.c_str(), &size)};
    GstSDPMessage* sdp{};
    if (gst_sdp_message_new(&sdp) != GST_SDP_OK)
        throw std::bad_alloc{};
    Description answer{gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER, sdp)};
    if (!size || gst_sdp_message_parse_buffer(bytes.get(), static_cast<guint>(size), answer->sdp) != GST_SDP_OK)
        throw std::runtime_error("Invalid SDP answer");
    set_description(rtc, "set-remote-description", answer.get());
}
} // namespace

int main()
try {
    gst_init(nullptr, nullptr);
    GError* err{};
    Element pipeline{gst_parse_launch(
        "webrtcbin name=rtc bundle-policy=max-bundle rtc. ! queue ! rtph265depay "
        "! h265parse ! avdec_h265 max-threads=2 ! appsink name=frames sync=false max-buffers=1 drop=true",
        &err)};
    Handle<GError, g_error_free> error{err};
    if (!pipeline || error)
        return 1;
    Element rtc{gst_bin_get_by_name(GST_BIN(pipeline.get()), "rtc")},
        sink{gst_bin_get_by_name(GST_BIN(pipeline.get()), "frames")};
    Handle<GstCaps, gst_caps_unref> caps{
        gst_caps_from_string("application/x-rtp,media=video,encoding-name=H265,clock-rate=90000,payload=100")};
    if (gst_element_set_state(pipeline.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        return 1;
    struct Stop {
        GstElement* pipeline;
        ~Stop()
        {
            gst_element_set_state(pipeline, GST_STATE_NULL);
        }
    } stop{pipeline.get()};
    GstWebRTCRTPTransceiver* channel{};
    g_signal_emit_by_name(rtc.get(), "add-transceiver", GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY, caps.get(),
                          &channel);
    Handle<GstWebRTCRTPTransceiver, gst_object_unref> stream{channel};
    if (!stream)
        return 1;
    exchange(rtc.get());
    Handle<GstSample, gst_sample_unref> sample{
        gst_app_sink_try_pull_sample(GST_APP_SINK(sink.get()), 10 * GST_SECOND)};
    if (!sample)
        return 1;
    const auto* video = gst_caps_get_structure(gst_sample_get_caps(sample.get()), 0);
    int width{}, height{};
    if (!gst_structure_get_int(video, "width", &width) || !gst_structure_get_int(video, "height", &height))
        return 1;
    std::cout << width << " " << height << std::endl;
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return 1;
}
