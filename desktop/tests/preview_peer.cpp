// Test camera offering a moving H.264 picture through the site's SDP exchange.
#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>

int main(int argc, char** argv)
{
    gst_init(&argc, &argv);
    if (argc != 2)
        return 1;
    std::string offer(std::stoul(argv[1]), '\0');
    std::cin.read(offer.data(), static_cast<std::streamsize>(offer.size()));
    GstSDPMessage* sdp{};
    if (gst_sdp_message_new_from_text(offer.c_str(), &sdp) != GST_SDP_OK)
        return 2;
    auto* pipeline =
        gst_parse_launch("videotestsrc is-live=true ! video/x-raw,width=320,height=180,framerate=15/1 ! x264enc "
                         "speed-preset=ultrafast tune=zerolatency key-int-max=15 ! h264parse ! rtph264pay "
                         "config-interval=-1 pt=96 ! "
                         "application/"
                         "x-rtp,media=video,encoding-name=H264,clock-rate=90000,packetization-mode=(string)1,"
                         "payload=(int)96 ! webrtcbin bundle-policy=max-bundle name=peer",
                         nullptr);
    auto* peer = gst_bin_get_by_name(GST_BIN(pipeline), "peer");
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    auto* remote = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp);
    auto* promise = gst_promise_new();
    g_signal_emit_by_name(peer, "set-remote-description", remote, promise);
    if (gst_promise_wait(promise) != GST_PROMISE_RESULT_REPLIED)
        return 3;
    gst_promise_unref(promise);
    gst_webrtc_session_description_free(remote);
    promise = gst_promise_new();
    g_signal_emit_by_name(peer, "create-answer", nullptr, promise);
    if (gst_promise_wait(promise) != GST_PROMISE_RESULT_REPLIED)
        return 4;
    GstWebRTCSessionDescription* answer{};
    if (!gst_structure_get(gst_promise_get_reply(promise), "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer,
                           nullptr))
        return 5;
    gst_promise_unref(promise);
    g_signal_emit_by_name(peer, "set-local-description", answer, nullptr);
    gst_webrtc_session_description_free(answer);
    for (int i = 0; i < 150; ++i) {
        GstWebRTCICEGatheringState state{};
        g_object_get(peer, "ice-gathering-state", &state, nullptr);
        if (state == GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    g_object_get(peer, "local-description", &answer, nullptr);
    auto* text = gst_sdp_message_as_text(answer->sdp);
    std::cout << std::char_traits<char>::length(text) << '\n' << text << std::flush;
    g_free(text);
    gst_webrtc_session_description_free(answer);
    std::cin.get();
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(peer);
    gst_object_unref(pipeline);
}
