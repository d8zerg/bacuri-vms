#include <glib-unix.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <gst/rtsp-server/rtsp-onvif-server.h>
#include <gst/rtsp-server/rtsp-onvif-media-factory.h>
#include <gst/rtsp-server/rtsp-onvif-media.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

namespace bacuri::vms::test
{
namespace
{

struct StreamProfile {
    const char* path;
    const char* pipeline;
};

constexpr StreamProfile main_stream{
    .path = "/main",
    .pipeline = "( videotestsrc is-live=true pattern=ball "
                "! video/x-raw,format=I420,width=1280,height=720,framerate=25/1 "
                "! timeoverlay "
                "! x264enc bitrate=2000 key-int-max=25 speed-preset=ultrafast tune=zerolatency "
                "! video/x-h264,profile=baseline "
                "! rtph264pay name=pay0 pt=96 config-interval=1 )"};

constexpr StreamProfile sub_stream{
    .path = "/sub",
    .pipeline = "( videotestsrc is-live=true pattern=smpte "
                "! video/x-raw,format=I420,width=640,height=360,framerate=15/1 "
                "! timeoverlay "
                "! x264enc bitrate=500 key-int-max=15 speed-preset=ultrafast tune=zerolatency "
                "! video/x-h264,profile=baseline "
                "! rtph264pay name=pay0 pt=96 config-interval=1 )"};

void mount_stream(GstRTSPMountPoints* mounts, const StreamProfile& profile)
{
    const bool audio = !g_strcmp0(g_getenv("BACURI_TEST_AUDIO"), "1") && std::string_view(profile.path) == "/main";
    auto* factory = audio ? gst_rtsp_onvif_media_factory_new() : gst_rtsp_media_factory_new();
    std::string launch{profile.pipeline};
    if (audio) {
        gst_rtsp_media_factory_set_media_gtype(factory, GST_TYPE_RTSP_ONVIF_MEDIA);
        gst_rtsp_onvif_media_factory_set_backchannel_launch(
            GST_RTSP_ONVIF_MEDIA_FACTORY(factory),
            "( capsfilter name=depay_backchannel "
            "caps=application/x-rtp,media=audio,encoding-name=PCMA,clock-rate=8000,payload=8 "
            "! rtppcmadepay ! alawdec ! audio/x-raw,format=S16LE ! filesink location=speaker.pcm append=true "
            "buffer-mode=unbuffered async=false sync=false )");
        launch.insert(launch.rfind(')'),
                      " audiotestsrc is-live=true freq=440 ! audioconvert ! audioresample ! "
                      "audio/x-raw,rate=8000,channels=1 ! alawenc ! rtppcmapay name=pay1 pt=8 ");
    }
    gst_rtsp_media_factory_set_launch(factory, launch.c_str());
    gst_rtsp_media_factory_set_shared(factory, TRUE);
    gst_rtsp_media_factory_add_role(factory, "viewer", GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
                                    GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, nullptr);
    gst_rtsp_mount_points_add_factory(mounts, profile.path, factory);
}

gboolean quit(gpointer data)
{
    g_main_loop_quit(static_cast<GMainLoop*>(data));
    return G_SOURCE_CONTINUE;
}

// Encode both profiles from the same V4L2 capture. RTP stays on container loopback.
constexpr auto webcam_capture =
    "v4l2src device=/dev/video0 ! decodebin ! videoconvert ! watchdog timeout=5000 ! tee name=capture "
    "capture. ! queue max-size-buffers=2 leaky=downstream ! videoscale ! videorate "
    "! video/x-raw,format=I420,width=1280,height=720,framerate=25/1 "
    "! x264enc bitrate=2000 key-int-max=25 speed-preset=ultrafast tune=zerolatency "
    "! video/x-h264,profile=baseline ! rtph264pay pt=96 config-interval=-1 "
    "! udpsink host=127.0.0.1 port=5000 sync=false async=false "
    "capture. ! queue max-size-buffers=2 leaky=downstream ! videoscale ! videorate "
    "! video/x-raw,format=I420,width=640,height=360,framerate=15/1 "
    "! x264enc bitrate=500 key-int-max=15 speed-preset=ultrafast tune=zerolatency "
    "! video/x-h264,profile=baseline ! rtph264pay pt=96 config-interval=-1 "
    "! udpsink host=127.0.0.1 port=5002 sync=false async=false";

std::string relay(unsigned port)
{
    return "( udpsrc address=127.0.0.1 port=" + std::to_string(port) +
           " caps=\"application/x-rtp,media=video,encoding-name=H264,clock-rate=90000,payload=96\" "
           "! rtpjitterbuffer mode=none latency=60 ! rtph264depay ! h264parse "
           "! rtph264pay name=pay0 pt=96 config-interval=-1 )";
}

struct CaptureState {
    GMainLoop* loop;
    bool failed{};
};

gboolean capture_message(GstBus*, GstMessage* message, gpointer data)
{
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR || GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
        auto& state = *static_cast<CaptureState*>(data);
        std::fputs("V4L2 capture failed or stopped delivering frames\n", stderr);
        state.failed = true;
        g_main_loop_quit(state.loop);
    }
    return G_SOURCE_CONTINUE;
}

int run_camera(bool webcam, const char* certificate_file)
{
    const std::unique_ptr<GstRTSPServer, decltype(&g_object_unref)> server{gst_rtsp_onvif_server_new(),
                                                                           g_object_unref};
    const std::unique_ptr<GstRTSPMountPoints, decltype(&g_object_unref)> mounts{
        gst_rtsp_server_get_mount_points(server.get()), g_object_unref};
    const std::unique_ptr<GMainLoop, decltype(&g_main_loop_unref)> loop{g_main_loop_new(nullptr, FALSE),
                                                                        g_main_loop_unref};

    gst_rtsp_server_set_service(server.get(), "8554");
    if (certificate_file) {
        std::unique_ptr<GTlsCertificate, decltype(&g_object_unref)> certificate{
            g_tls_certificate_new_from_file(certificate_file, nullptr), g_object_unref};
        if (!certificate) {
            std::fputs("Cannot load RTSP test certificate and key\n", stderr);
            return EXIT_FAILURE;
        }
        std::unique_ptr<GstRTSPAuth, decltype(&g_object_unref)> auth{gst_rtsp_auth_new(), g_object_unref};
        gst_rtsp_auth_set_tls_certificate(auth.get(), certificate.get());
        auto* token = gst_rtsp_token_new(GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING, "viewer", nullptr);
        gst_rtsp_auth_set_default_token(auth.get(), token);
        gst_rtsp_token_unref(token);
        gst_rtsp_server_set_auth(server.get(), auth.get());
    }
    std::unique_ptr<GstElement, decltype(&gst_object_unref)> capture{nullptr, gst_object_unref};
    CaptureState state{loop.get()};
    guint bus_watch{};
    if (webcam) {
        GError* error{};
        capture.reset(gst_parse_launch(webcam_capture, &error));
        if (error || !capture) {
            g_clear_error(&error);
            std::fputs("Cannot construct the V4L2 capture pipeline\n", stderr);
            return EXIT_FAILURE;
        }
        std::unique_ptr<GstBus, decltype(&gst_object_unref)> bus{gst_element_get_bus(capture.get()),
                                                                 gst_object_unref};
        bus_watch = gst_bus_add_watch(bus.get(), capture_message, &state);
        if (gst_element_set_state(capture.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
            std::fputs("Cannot open V4L2 capture device\n", stderr);
            gst_element_set_state(capture.get(), GST_STATE_NULL);
            g_source_remove(bus_watch);
            return EXIT_FAILURE;
        }
        const auto main_relay = relay(5000);
        const auto sub_relay = relay(5002);
        mount_stream(mounts.get(), {"/main", main_relay.c_str()});
        mount_stream(mounts.get(), {"/sub", sub_relay.c_str()});
    } else {
        mount_stream(mounts.get(), main_stream);
        mount_stream(mounts.get(), sub_stream);
        for (int channel = 1; channel <= 4; ++channel) {
            for (const auto& profile : {main_stream, sub_stream}) {
                auto path = "/channel" + std::to_string(channel) + profile.path;
                mount_stream(mounts.get(), {path.c_str(), profile.pipeline});
            }
        }
        const auto hevc_profile = [](const char* size) {
            return std::string{"( videotestsrc is-live=true pattern=ball ! video/x-raw,format=I420,"} + size +
                   ",framerate=15/1 ! x265enc bitrate=640 key-int-max=15 tune=zerolatency speed-preset=ultrafast "
                   "option-string=pools=1 ! video/x-h265,profile=main ! rtph265pay name=pay0 pt=98 "
                   "config-interval=1 )";
        };
        const auto main_hevc = hevc_profile("width=640,height=360"),
                   sub_hevc = hevc_profile("width=320,height=180");
        mount_stream(mounts.get(), {"/hevc", main_hevc.c_str()});
        mount_stream(mounts.get(), {"/hevc-sub", sub_hevc.c_str()});
        mount_stream(mounts.get(),
                     {"/jpeg", "( videotestsrc is-live=true ! video/x-raw,format=I420,width=320,height=240 "
                               "! jpegenc ! rtpjpegpay name=pay0 pt=26 )"});
    }

    const auto server_source = gst_rtsp_server_attach(server.get(), nullptr);
    if (server_source != 0) {
        const auto sigint_source = g_unix_signal_add(SIGINT, quit, loop.get());
        const auto sigterm_source = g_unix_signal_add(SIGTERM, quit, loop.get());
        g_main_loop_run(loop.get());
        g_source_remove(sigterm_source);
        g_source_remove(sigint_source);
        g_source_remove(server_source);
    } else {
        std::fputs("Could not listen on RTSP port 8554\n", stderr);
        state.failed = true;
    }
    if (capture) {
        gst_element_set_state(capture.get(), GST_STATE_NULL);
        g_source_remove(bus_watch);
    }
    return state.failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

} // namespace
} // namespace bacuri::vms::test

int main(int argc, char** argv)
{
    const bool webcam = argc == 2 && std::string_view{argv[1]} == "--webcam";
    const bool tls = argc == 3 && std::string_view{argv[1]} == "--tls";
    if (argc != 1 && !webcam && !tls) {
        std::fputs("Usage: bacuri-vms-test-rtsp-camera [--webcam | --tls PEM]\n", stderr);
        return EXIT_FAILURE;
    }
    gst_init(nullptr, nullptr);
    return bacuri::vms::test::run_camera(webcam, tls ? argv[2] : nullptr);
}
