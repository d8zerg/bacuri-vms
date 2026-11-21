#include <gio/gio.h>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/rtsp/gstrtspmessage.h>
#include <gst/rtsp/gstrtsptransport.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>

namespace
{
gboolean allow_configured_uri(GstElement* source, GstRTSPMessage*, gpointer data)
{
    gchar* location{};
    g_object_get(source, "location", &location, nullptr);
    const bool allowed = g_strcmp0(location, static_cast<const char*>(data)) == 0;
    g_free(location);
    return allowed;
}
} // namespace

// Read the URI and credentials from the test environment so they stay out of argv.
int main(int argc, char** argv)
{
    auto* uri = std::getenv("BACURI_FRAME_URI");
    if (argc != 2 || uri == nullptr || !std::string_view{uri}.starts_with("rtsps://")) {
        return EXIT_FAILURE;
    }
    gst_init(nullptr, nullptr);
    std::unique_ptr<GTlsDatabase, decltype(&g_object_unref)> trust{g_tls_file_database_new(argv[1], nullptr),
                                                                   g_object_unref};
    std::unique_ptr<GstElement, decltype(&gst_object_unref)> pipeline{
        gst_parse_launch("rtspsrc name=camera ! application/x-rtp,media=video,encoding-name=H264 ! "
                         "rtph264depay ! h264parse ! avdec_h264 ! appsink name=frames max-buffers=1 drop=true",
                         nullptr),
        gst_object_unref};
    if (!pipeline || !trust) {
        return EXIT_FAILURE;
    }
    std::unique_ptr<GstElement, decltype(&gst_object_unref)> source{
        gst_bin_get_by_name(GST_BIN(pipeline.get()), "camera"), gst_object_unref};
    std::unique_ptr<GstElement, decltype(&gst_object_unref)> sink{
        gst_bin_get_by_name(GST_BIN(pipeline.get()), "frames"), gst_object_unref};
    g_signal_connect(source.get(), "before-send", G_CALLBACK(allow_configured_uri), uri);
    g_object_set(source.get(), "location", uri, "tls-database", trust.get(), "protocols", GST_RTSP_LOWER_TRANS_TCP,
                 "user-id", std::getenv("BACURI_CAMERA_USER"), "user-pw", std::getenv("BACURI_CAMERA_PASSWORD"),
                 nullptr);
    gst_element_set_state(pipeline.get(), GST_STATE_PLAYING);
    std::unique_ptr<GstSample, decltype(&gst_sample_unref)> frame{
        gst_app_sink_try_pull_sample(GST_APP_SINK(sink.get()), 10 * GST_SECOND), gst_sample_unref};
    gst_element_set_state(pipeline.get(), GST_STATE_NULL);
    if (!frame) {
        return EXIT_FAILURE;
    }
    const auto* caps = gst_caps_get_structure(gst_sample_get_caps(frame.get()), 0);
    int width{};
    int height{};
    if (!gst_structure_get_int(caps, "width", &width) || !gst_structure_get_int(caps, "height", &height)) {
        return EXIT_FAILURE;
    }
    std::printf("%d %d\n", width, height);
    return EXIT_SUCCESS;
}
