#include <glib-unix.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace bacuri::vms::test {
namespace {

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
                "! rtph264pay name=pay0 pt=96 config-interval=1 )"
};

constexpr StreamProfile sub_stream{
    .path = "/sub",
    .pipeline = "( videotestsrc is-live=true pattern=smpte "
                "! video/x-raw,format=I420,width=640,height=360,framerate=15/1 "
                "! timeoverlay "
                "! x264enc bitrate=500 key-int-max=15 speed-preset=ultrafast tune=zerolatency "
                "! video/x-h264,profile=baseline "
                "! rtph264pay name=pay0 pt=96 config-interval=1 )"
};

void mount_stream(GstRTSPMountPoints* mounts, const StreamProfile& profile)
{
    auto* factory = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(factory, profile.pipeline);
    gst_rtsp_media_factory_set_shared(factory, TRUE);
    gst_rtsp_mount_points_add_factory(mounts, profile.path, factory);
}

gboolean quit(gpointer data)
{
    g_main_loop_quit(static_cast<GMainLoop*>(data));
    return G_SOURCE_CONTINUE;
}

int run_camera()
{
    const std::unique_ptr<GstRTSPServer, decltype(&g_object_unref)> server{
        gst_rtsp_server_new(), g_object_unref
    };
    const std::unique_ptr<GstRTSPMountPoints, decltype(&g_object_unref)> mounts{
        gst_rtsp_server_get_mount_points(server.get()), g_object_unref
    };
    const std::unique_ptr<GMainLoop, decltype(&g_main_loop_unref)> loop{
        g_main_loop_new(nullptr, FALSE), g_main_loop_unref
    };

    gst_rtsp_server_set_service(server.get(), "8554");
    mount_stream(mounts.get(), main_stream);
    mount_stream(mounts.get(), sub_stream);

    const auto server_source = gst_rtsp_server_attach(server.get(), nullptr);
    if (server_source == 0) {
        std::fputs("Could not listen on RTSP port 8554\n", stderr);
        return EXIT_FAILURE;
    }

    const auto sigint_source = g_unix_signal_add(SIGINT, quit, loop.get());
    const auto sigterm_source = g_unix_signal_add(SIGTERM, quit, loop.get());
    g_main_loop_run(loop.get());
    g_source_remove(sigterm_source);
    g_source_remove(sigint_source);
    g_source_remove(server_source);
    return EXIT_SUCCESS;
}

}  // namespace
}  // namespace bacuri::vms::test

int main()
{
    gst_init(nullptr, nullptr);
    return bacuri::vms::test::run_camera();
}
