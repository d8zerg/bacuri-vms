#include "media/privacy.hpp"
#include <gst/video/video.h>
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
namespace bacuri::vms::media
{
PrivacyMask::PrivacyMask(std::string_view json)
{
    if (json.empty())
        return;
    const auto items = nlohmann::json::parse(json);
    if (!items.is_array() || items.size() > 32)
        throw std::invalid_argument("Invalid video mask");
    for (const auto& item : items) {
        const Region r{item.at("left"), item.at("top"), item.at("right"), item.at("bottom")};
        if (!(r.left >= 0 && r.top >= 0 && r.right <= 1 && r.bottom <= 1 && r.left < r.right && r.top < r.bottom))
            throw std::invalid_argument("Invalid video mask bounds");
        regions_.push_back(r);
    }
}
bool PrivacyMask::cover(GstPad* pad, GstPadProbeInfo* probe) const
{
    std::unique_ptr<GstCaps, decltype(&gst_caps_unref)> caps{gst_pad_get_current_caps(pad), gst_caps_unref};
    GstVideoInfo info{};
    if (!caps || !gst_video_info_from_caps(&info, caps.get()) ||
        GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_I420)
        return false;
    auto* buffer = gst_buffer_make_writable(GST_PAD_PROBE_INFO_BUFFER(probe));
    GST_PAD_PROBE_INFO_DATA(probe) = buffer;
    GstVideoFrame frame{};
    if (!buffer || !gst_video_frame_map(&frame, &info, buffer, GST_MAP_WRITE))
        return false;
    for (int plane = 0; plane < 3; ++plane) {
        const int width = GST_VIDEO_FRAME_COMP_WIDTH(&frame, plane),
                  height = GST_VIDEO_FRAME_COMP_HEIGHT(&frame, plane);
        const auto stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, plane);
        auto* pixels = static_cast<unsigned char*>(GST_VIDEO_FRAME_PLANE_DATA(&frame, plane));
        if (!pixels || width <= 0 || height <= 0 || stride < width) {
            gst_video_frame_unmap(&frame);
            return false;
        }
        for (const auto& r : regions_) {
            const int x = static_cast<int>(std::floor(r.left * width)),
                      end_x = static_cast<int>(std::ceil(r.right * width));
            const int y = static_cast<int>(std::floor(r.top * height)),
                      end_y = static_cast<int>(std::ceil(r.bottom * height));
            for (int row = y; row < end_y; ++row)
                std::memset(pixels + static_cast<std::ptrdiff_t>(row) * stride + x, plane ? 128 : 16,
                            static_cast<std::size_t>(end_x - x));
        }
    }
    gst_video_frame_unmap(&frame);
    return true;
}
void PrivacyMask::attach(GstElement* encoder)
{
    if (empty())
        return;
    if (!encoder)
        throw std::runtime_error("Video masking unavailable");
    std::unique_ptr<GstPad, decltype(&gst_object_unref)> pad{gst_element_get_static_pad(encoder, "sink"),
                                                             gst_object_unref};
    if (!pad || !gst_pad_add_probe(
                    pad.get(), GST_PAD_PROBE_TYPE_BUFFER,
                    [](GstPad* p, GstPadProbeInfo* probe, gpointer data) {
                        if (static_cast<const PrivacyMask*>(data)->cover(p, probe))
                            return GST_PAD_PROBE_OK;
                        GST_ELEMENT_ERROR(GST_ELEMENT(GST_OBJECT_PARENT(p)), STREAM, FAILED,
                                          ("Video masking failed"), (nullptr));
                        return GST_PAD_PROBE_DROP;
                    },
                    this, nullptr))
        throw std::runtime_error("Cannot install video mask");
}
} // namespace bacuri::vms::media
