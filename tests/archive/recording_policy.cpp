#include "../recording/schedule.hpp"
#include <gtest/gtest.h>
#include "archive/writer.hpp"
#include <gst/app/gstappsink.h>
namespace bacuri::vms::archive
{
namespace
{
using namespace std::chrono;
using namespace std::chrono_literals;

std::vector<std::byte> one_sample(const std::string& description)
{
    GError* error{};
    auto* pipeline = gst_parse_launch(description.c_str(), &error);
    if (error || !pipeline) {
        std::string reason = error ? error->message : "Missing media pipeline";
        g_clear_error(&error);
        if (pipeline)
            gst_object_unref(pipeline);
        throw std::runtime_error(reason);
    }
    auto* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sample");
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    auto* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 5 * GST_SECOND);
    std::vector<std::byte> data;
    if (sample) {
        auto* buffer = gst_sample_get_buffer(sample);
        data.resize(gst_buffer_get_size(buffer));
        gst_buffer_extract(buffer, 0, data.data(), data.size());
        gst_sample_unref(sample);
    }
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(sink);
    gst_object_unref(pipeline);
    if (data.empty())
        throw std::runtime_error("Missing encoded or decoded video frame");
    return data;
}

TEST(RecordingPolicy, ArchiveDecodesNumberedSamplesAcrossTheRepeatedHour)
{
    gst_init(nullptr, nullptr);
    struct TemporaryVolume {
        fs::path root = fs::temp_directory_path() / uuid();
        TemporaryVolume() = default;
        TemporaryVolume(const TemporaryVolume&) = delete;
        TemporaryVolume& operator=(const TemporaryVolume&) = delete;
        ~TemporaryVolume()
        {
            std::error_code error;
            fs::remove_all(root, error);
        }
    } temp;
    fs::create_directory(temp.root);
    Volume volume{temp.root, {uuid(), uuid()}};
    const Origin origin{uuid(), uuid(), uuid(), uuid(), 1};
    const auto rule = recording::test::weekly();
    recording::RecordingPlan plan{temp.root};
    plan.update(Json{{origin.point,
                      {{"id", uuid()}, {"revision", 1}, {"since", 0}, {"rule", rule}, {"windows", Json::array()}}}}
                    .dump());
    auto queue = std::make_shared<ingest::FrameQueue>(ingest::Profile::main);
    const auto day = sys_days{2025y / October / 26};
    std::map<std::int64_t, int> expected;
    const std::array offsets = {9min, 10min, 29min, 30min, 69min, 70min, 89min, 90min};
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        const auto intensity = 24 + 28 * i;
        const auto color = 0xff000000u + intensity * 0x010101u;
        auto bytes =
            one_sample("videotestsrc num-buffers=1 pattern=solid-color foreground-color=" + std::to_string(color) +
                       " ! video/x-raw,width=32,height=32,format=I420 ! x264enc tune=zerolatency key-int-max=1 "
                       "byte-stream=true ! video/x-h264,stream-format=byte-stream,alignment=au ! appsink "
                       "name=sample sync=false");
        domain::UtcTime utc = day + offsets[i];
        queue->push(std::make_shared<ingest::Frame>(ingest::Frame{
            std::move(bytes), domain::ProjectedTime::from_reception(utc), {}, {}, 0, true, 40000000}));
        if (i == 1 || i == 2 || i == 5 || i == 6)
            expected[utc.time_since_epoch().count()] = static_cast<int>(intensity);
    }
    {
        Writer writer{volume, plan, origin, queue};
    }
    std::size_t count = 0;
    volume.journal.visit_inventory([&](const BlockManifest& manifest, const std::string& unavailable) {
        EXPECT_TRUE(unavailable.empty());
        const auto at = manifest.recording.interval().begin().time_since_epoch().count();
        ASSERT_TRUE(expected.contains(at));
        auto path = temp.root / "blocks" / manifest.recording.block().id.to_string() / "media.mp4";
        auto decoded = one_sample("filesrc location=" + path.string() +
                                  " ! qtdemux ! h264parse ! avdec_h264 ! videoconvert ! video/x-raw,format=GRAY8 "
                                  "! appsink name=sample sync=false");
        EXPECT_NEAR(std::to_integer<int>(decoded.at(0)), expected.at(at), 14);
        expected.erase(at);
        ++count;
    });
    EXPECT_EQ(count, 4u);
    EXPECT_TRUE(expected.empty());
}
} // namespace
} // namespace bacuri::vms::archive
