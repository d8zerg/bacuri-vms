#include "archive/writer.hpp"

#include <gst/app/gstappsink.h>
#include <gtest/gtest.h>

namespace bacuri::vms::archive
{
namespace
{
using namespace std::chrono_literals;

struct EncodedFrame {
    std::vector<std::byte> bytes;
    bool keyframe;
};

// Encodes `count` frames with a single keyframe at the start.
std::vector<EncodedFrame> encoded_frames(int count)
{
    const auto description =
        "videotestsrc num-buffers=" + std::to_string(count) +
        " ! video/x-raw,width=32,height=32,format=I420,framerate=25/1 ! x264enc "
        "tune=zerolatency key-int-max=1000 byte-stream=true ! "
        "video/x-h264,stream-format=byte-stream,alignment=au ! appsink name=sample sync=false";
    GError* error{};
    std::unique_ptr<GstElement, decltype(&gst_object_unref)> pipeline{
        gst_parse_launch(description.c_str(), &error), gst_object_unref};
    if (error || !pipeline) {
        std::string reason = error ? error->message : "Missing encoder pipeline";
        g_clear_error(&error);
        throw std::runtime_error(reason);
    }
    std::unique_ptr<GstElement, decltype(&gst_object_unref)> sink{
        gst_bin_get_by_name(GST_BIN(pipeline.get()), "sample"), gst_object_unref};
    gst_element_set_state(pipeline.get(), GST_STATE_PLAYING);
    std::vector<EncodedFrame> frames;
    while (static_cast<int>(frames.size()) < count) {
        auto* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink.get()), 5 * GST_SECOND);
        if (!sample)
            break;
        auto* buffer = gst_sample_get_buffer(sample);
        EncodedFrame frame{std::vector<std::byte>(gst_buffer_get_size(buffer)),
                           !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT)};
        gst_buffer_extract(buffer, 0, frame.bytes.data(), frame.bytes.size());
        gst_sample_unref(sample);
        frames.push_back(std::move(frame));
    }
    gst_element_set_state(pipeline.get(), GST_STATE_NULL);
    if (static_cast<int>(frames.size()) != count)
        throw std::runtime_error("Missing encoded frames");
    return frames;
}

Json continuous_rule()
{
    return {{"continuous", true}, {"manual", false}, {"event", false}, {"audio", false},           {"pre", 0},
            {"post", 1},          {"days", nullptr}, {"zone", "UTC"},  {"schedule", Json::array()}};
}

TEST(ArchiveWriter, FramesDiscardedAfterTheBlockLimitAreJournaledAsKeyframeGap)
{
    gst_init(nullptr, nullptr);
    struct TemporaryVolume {
        fs::path root = fs::temp_directory_path() / uuid();
        TemporaryVolume()
        {
            fs::create_directory(root);
        }
        TemporaryVolume(const TemporaryVolume&) = delete;
        TemporaryVolume& operator=(const TemporaryVolume&) = delete;
        ~TemporaryVolume()
        {
            std::error_code error;
            fs::remove_all(root, error);
        }
    } temp;
    Volume volume{temp.root, {uuid(), uuid()}};
    const Origin origin{uuid(), uuid(), uuid(), uuid(), 1};
    volume.update_recording_plan(Json{
        {origin.point,
         {{"id", uuid()}, {"revision", 1}, {"since", 0}, {"rule", continuous_rule()}, {"windows", Json::array()}}}}
                                     .dump());
    // One frame per second: the block reaches its duration limit at the 17th frame.
    const auto long_group = encoded_frames(20);
    const auto next_group = encoded_frames(1);
    ASSERT_TRUE(long_group.front().keyframe && !long_group.back().keyframe && next_group.front().keyframe);
    const domain::UtcTime start{1'800'000'000'000'000us};
    auto queue = std::make_shared<ingest::FrameQueue>(ingest::Profile::main);
    const auto push = [&](const EncodedFrame& frame, std::chrono::seconds offset) {
        queue->push(std::make_shared<ingest::Frame>(
            ingest::Frame{frame.bytes,
                          domain::ProjectedTime::from_reception(start + offset),
                          {},
                          {},
                          static_cast<std::uint64_t>(std::chrono::nanoseconds{offset}.count()),
                          frame.keyframe,
                          40'000'000}));
    };
    for (std::size_t i = 0; i < long_group.size(); ++i)
        push(long_group[i], std::chrono::seconds{static_cast<std::int64_t>(i)});
    push(next_group.front(), 20s);
    {
        Writer writer{volume, origin, queue};
    }

    std::vector<Gap> gaps;
    volume.visit_gaps([&](const Gap& gap) { gaps.push_back(gap); });
    ASSERT_EQ(gaps.size(), 1u);
    EXPECT_EQ(gaps.front().reason, "keyframe_interval");
    EXPECT_EQ(gaps.front().point, origin.point);
    EXPECT_EQ(gaps.front().begin, (start + 16s).time_since_epoch().count());
    EXPECT_EQ(gaps.front().end, (start + 20s).time_since_epoch().count());

    std::vector<std::int64_t> blocks;
    volume.visit_inventory([&](const Json& manifest, const std::string& unavailable) {
        EXPECT_TRUE(unavailable.empty());
        blocks.push_back(manifest.at("begin_utc_us"));
    });
    std::ranges::sort(blocks);
    ASSERT_EQ(blocks.size(), 2u);
    EXPECT_EQ(blocks.front(), start.time_since_epoch().count());
    EXPECT_EQ(blocks.back(), (start + 20s).time_since_epoch().count());
}
} // namespace
} // namespace bacuri::vms::archive
