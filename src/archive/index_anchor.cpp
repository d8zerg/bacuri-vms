#include "archive/index_anchor.hpp"
#include "archive/analysis.hpp"
#include "media/acceleration/codec_task.hpp"
#include "platform/scope_exit.hpp"
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <chrono>
#include <array>

namespace bacuri::vms::archive
{
namespace
{
using namespace std::chrono_literals;
template <class T> auto own(T* p)
{
    return std::unique_ptr<T, decltype(&gst_object_unref)>{p, gst_object_unref};
}
using Sample = std::unique_ptr<GstSample, decltype(&gst_sample_unref)>;
const std::string scale =
    " ! videoconvert ! videoscale ! video/x-raw,format=GRAY8,width=64,height=48,pixel-aspect-ratio=1/1";
// Keep the anchor in ordinary memory so returning it cannot retain a hardware decoder surface or its pool.
Sample copy_picture(GstSample* sample)
{
    auto* caps = gst_sample_get_caps(sample);
    auto* buffer = gst_sample_get_buffer(sample);
    GstVideoInfo info{};
    if (!gst_video_info_from_caps(&info, caps))
        throw acceleration::CodecFailure{"Invalid decoded keyframe"};
    std::unique_ptr<GstBuffer, decltype(&gst_buffer_unref)> copy{
        gst_buffer_new_allocate(nullptr, info.size, nullptr), gst_buffer_unref};
    GstVideoFrame source{}, destination{};
    if (!copy || !gst_video_frame_map(&source, &info, buffer, GST_MAP_READ))
        throw acceleration::CodecFailure{"Unreadable decoded keyframe"};
    platform::ScopeExit release_source{[&] { gst_video_frame_unmap(&source); }};
    if (!gst_video_frame_map(&destination, &info, copy.get(), GST_MAP_WRITE))
        throw acceleration::CodecFailure{"Cannot copy decoded keyframe"};
    platform::ScopeExit release_destination{[&] { gst_video_frame_unmap(&destination); }};
    if (!gst_video_frame_copy(&destination, &source))
        throw acceleration::CodecFailure{"Cannot copy decoded keyframe"};
    GST_BUFFER_PTS(copy.get()) = GST_BUFFER_PTS(buffer);
    return Sample{gst_sample_new(copy.get(), caps, gst_sample_get_segment(sample), nullptr), gst_sample_unref};
}
Sample first_picture(const std::filesystem::path& file, const std::string& codec, acceleration::CodecTask& task,
                     const acceleration::CodecPath& path, bool thumbnail, GstSample* decoded = nullptr)
{
    const bool reuse = decoded && path.grant.empty();
    std::string chain = reuse ? "appsrc name=source format=time"
                              : "filesrc name=source ! qtdemux name=demux demux.video_0 ! " + codec + "parse ! " +
                                    path.decoder(codec);
    chain += thumbnail ? scale : " ! video/x-raw";
    // One random access picture supplies the block anchor. Bound read-ahead independently of block length.
    chain += " ! appsink name=picture max-buffers=1 sync=false";
    GError* raw{};
    auto pipe = own(gst_parse_launch(chain.c_str(), &raw));
    std::unique_ptr<GError, decltype(&g_error_free)> error{raw, g_error_free};
    if (!pipe || error)
        throw acceleration::CodecFailure{"Archive keyframe pipeline unavailable"};
    platform::ScopeExit stop{[&] { gst_element_set_state(pipe.get(), GST_STATE_NULL); }};
    auto src = own(gst_bin_get_by_name(GST_BIN(pipe.get()), "source"));
    auto sink = own(gst_bin_get_by_name(GST_BIN(pipe.get()), "picture"));
    auto bus = own(gst_element_get_bus(pipe.get()));
    if (reuse)
        gst_app_src_set_caps(GST_APP_SRC(src.get()), gst_sample_get_caps(decoded));
    else
        g_object_set(src.get(), "location", file.c_str(), nullptr);
    if (gst_element_set_state(pipe.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        throw acceleration::CodecFailure{"Cannot start keyframe pipeline"};
    if (reuse && (gst_app_src_push_buffer(GST_APP_SRC(src.get()),
                                          gst_buffer_ref(gst_sample_get_buffer(decoded))) != GST_FLOW_OK ||
                  gst_app_src_end_of_stream(GST_APP_SRC(src.get())) != GST_FLOW_OK))
        throw std::runtime_error{"Cannot scale keyframe"};
    const auto end = std::chrono::steady_clock::now() + 5s;
    do {
        task.check(path);
        Sample sample{gst_app_sink_try_pull_sample(GST_APP_SINK(sink.get()), GST_SECOND / 10), gst_sample_unref};
        if (sample && GST_BUFFER_PTS_IS_VALID(gst_sample_get_buffer(sample.get()))) {
            task.used(path);
            if (!path.grant.empty() && !thumbnail)
                return copy_picture(sample.get());
            return sample;
        }
        if (auto* message = gst_bus_pop_filtered(bus.get(), GST_MESSAGE_ERROR)) {
            platform::ScopeExit release{[&] { gst_message_unref(message); }};
            acceleration::pipeline_error(message);
        }
        if (gst_app_sink_is_eos(GST_APP_SINK(sink.get())))
            break;
    } while (std::chrono::steady_clock::now() < end);
    throw acceleration::CodecFailure{"Missing archive keyframe"};
}
} // namespace
IndexAnchor index_anchor(const std::filesystem::path& file, const std::string& codec)
{
    gst_init(nullptr, nullptr);
    Sample raw{nullptr, gst_sample_unref};
    {
        acceleration::CodecTask keyframe{"keyframe"};
        raw = acceleration::process_block(
            keyframe, [&](const auto& path) { return first_picture(file, codec, keyframe, path, false); });
    }
    const auto pts = GST_BUFFER_PTS(gst_sample_get_buffer(raw.get()));
    acceleration::CodecTask thumbnail{"thumbnail"};
    auto image = acceleration::process_block(
        thumbnail, [&](const auto& path) { return first_picture(file, codec, thumbnail, path, true, raw.get()); });
    std::array<guchar, std::size_t{preview_width} * preview_height> pixels{};
    auto* buf = gst_sample_get_buffer(image.get());
    if (GST_BUFFER_PTS(buf) != pts || gst_buffer_extract(buf, 0, pixels.data(), pixels.size()) != pixels.size())
        throw std::runtime_error{"Archive thumbnail does not match its keyframe"};
    std::unique_ptr<gchar, decltype(&g_free)> encoded{g_base64_encode(pixels.data(), pixels.size()), g_free};
    return {static_cast<std::int64_t>(pts / GST_USECOND), encoded.get()};
}
} // namespace bacuri::vms::archive
