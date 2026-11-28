#include "media/native_frame.hpp"
#include <QVariant>
#include <gst/video/video.h>

namespace bacuri::vms::client
{
namespace
{
NativeFrame::Sample downloadFrame(GstSample* source, const GstVideoInfo& info)
{
    GstVideoFrame input{};
    if (!gst_video_frame_map(&input, &info, gst_sample_get_buffer(source), GST_MAP_READ))
        return {};
    auto* buffer = gst_buffer_new_allocate(nullptr, info.size, nullptr);
    GstVideoFrame output{};
    const bool mapped = buffer && gst_video_frame_map(&output, &info, buffer, GST_MAP_WRITE);
    const bool copied = mapped && gst_video_frame_copy(&output, &input);
    if (mapped)
        gst_video_frame_unmap(&output);
    gst_video_frame_unmap(&input);

    NativeFrame::Sample sample;
    if (copied) {
        GST_BUFFER_PTS(buffer) = GST_BUFFER_PTS(gst_sample_get_buffer(source));
        auto* caps = gst_video_info_to_caps(&info);
        sample = NativeFrame::Sample(gst_sample_new(buffer, caps, nullptr, nullptr), gst_sample_unref);
        gst_caps_unref(caps);
    }
    if (buffer)
        gst_buffer_unref(buffer);
    return sample;
}
} // namespace

NativeFrame* NativeFrame::source(QObject* item)
{
    return item ? qobject_cast<NativeFrame*>(item->property("decodedFrames").value<QObject*>()) : nullptr;
}
NativeFrame::~NativeFrame()
{
    attach(nullptr);
}
void NativeFrame::attach(QObject* item)
{
    if (target_ == item)
        return;
    if (target_ && target_->property("decodedFrames").value<QObject*>() == this)
        target_->setProperty("decodedFrames", {});
    target_ = item;
    if (item)
        item->setProperty("decodedFrames", QVariant::fromValue<QObject*>(this));
}
void NativeFrame::receive(GstPad* pad, GstBuffer* buffer)
{
    if (!buffer || !GST_BUFFER_PTS_IS_VALID(buffer))
        return;
    auto* caps = gst_pad_get_current_caps(pad);
    if (!caps)
        return;
    Sample next(gst_sample_new(buffer, caps, nullptr, nullptr), gst_sample_unref);
    gst_caps_unref(caps);
    std::scoped_lock lock(mutex_);
    sample_.swap(next);
}
void NativeFrame::clear()
{
    std::scoped_lock lock(mutex_);
    sample_.reset();
}
NativeFrame::Sample NativeFrame::sample() const
{
    std::scoped_lock lock(mutex_);
    return sample_;
}
NativeFrame::Picture NativeFrame::decode(const Sample& source)
{
    if (!source)
        return {};
    Sample converted = source;
    auto* caps = gst_sample_get_caps(source.get());
    GstVideoInfo original{};
    if (!gst_video_info_from_caps(&original, caps))
        return {};
    if (!gst_caps_features_contains(gst_caps_get_features(caps, 0), GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY))
        converted = downloadFrame(source.get(), original);
    if (!converted)
        return {};
    if (GST_VIDEO_INFO_FORMAT(&original) != GST_VIDEO_FORMAT_RGBA) {
        auto* format = gst_caps_from_string("video/x-raw,format=RGBA");
        auto* result = gst_video_convert_sample(converted.get(), format, 2 * GST_SECOND, nullptr);
        gst_caps_unref(format);
        if (!result)
            return {};
        converted = Sample(result, gst_sample_unref);
    }
    GstVideoInfo info{};
    GstVideoFrame frame{};
    if (!gst_video_info_from_caps(&info, gst_sample_get_caps(converted.get())) ||
        !gst_video_frame_map(&frame, &info, gst_sample_get_buffer(converted.get()), GST_MAP_READ))
        return {};
    QImage view(static_cast<const uchar*>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0)), GST_VIDEO_FRAME_WIDTH(&frame),
                GST_VIDEO_FRAME_HEIGHT(&frame), GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0), QImage::Format_RGBA8888);
    Picture result{view.copy(), GST_BUFFER_PTS(gst_sample_get_buffer(source.get()))};
    gst_video_frame_unmap(&frame);
    return result;
}
} // namespace bacuri::vms::client
