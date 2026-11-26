#include "application/export/periods.hpp"
#include "application/export/package.hpp"
#include <gmock/gmock.h>
#include <nlohmann/json.hpp>
namespace bacuri::vms::control
{
TEST(ExportManifest, RecordsOnlyNonemptyGapsWithTheirReason)
{
    std::vector<ExportPeriod> periods;
    add_gap(periods, 10, 10, "missing");
    add_gap(periods, 11, 10, "missing");
    EXPECT_TRUE(periods.empty());
    add_gap(periods, 10, 15, "missing");
    ASSERT_EQ(periods.size(), 1u);
    const auto* gap = std::get_if<ExportGap>(&periods.front());
    ASSERT_NE(gap, nullptr);
    EXPECT_EQ(gap->begin, 10);
    EXPECT_EQ(gap->end, 15);
    EXPECT_EQ(gap->reason, "missing");
}
TEST(ExportManifest, SelectionUsesTheVersionOneWireFormatWithoutAMode)
{
    FragmentManifest manifest{"site", "point",     "export", {},    10,
                              15,     "selection", {},       false, {ExportGap{10, 15, "missing"}}};
    const nlohmann::json expected{
        {"format", "bacuri.vms.export.v1"},
        {"site_id", "site"},
        {"point_id", "point"},
        {"export_id", "export"},
        {"begin_utc_us", 10},
        {"end_utc_us", 15},
        {"periods", {{{"state", "gap"}, {"begin_utc_us", 10}, {"end_utc_us", 15}, {"reason", "missing"}}}}};
    EXPECT_EQ(nlohmann::json::parse(encode_fragment_manifest(manifest)), expected);
}
TEST(ExportManifest, IncludesTheClipTransformMasksAndAudioInTheVersionTwoSignedPayload)
{
    using namespace std::chrono_literals;
    const domain::UtcTime start{100us};
    const archive::Block block{{archive::uuid(), archive::uuid(), archive::uuid(), archive::uuid(), 1},
                               domain::ProjectedTime::from_reception(start),
                               start + 1s};
    auto source = archive::make_manifest({{archive::uuid(), archive::uuid()}, archive::uuid(), 1}, archive::uuid(),
                                         block, {500, std::string(64, 'a')});
    ExportClip clip{std::move(source),
                    "clip.mp4",
                    "h264_masked_selection",
                    {200, "digest"},
                    100,
                    1000100,
                    25,
                    ExportPresentation{"h264",
                                       {{0, 0, 1, 1}},
                                       ExportedAudio{"audio.wav", {16044, "audio-digest"}, 100, 1000100}}};
    const FragmentManifest manifest{"site",  "point",      "export", "alert", 100,
                                    1000100, "derivative", "case",   true,    {std::move(clip)}};
    const auto result = nlohmann::json::parse(encode_fragment_manifest(manifest));
    EXPECT_EQ(result.at("format"), "bacuri.vms.export.v2");
    EXPECT_EQ(result.at("mode"), "derivative");
    EXPECT_EQ(result.at("reason"), "case");
    EXPECT_EQ(result.at("audio_requested"), true);
    EXPECT_EQ(result.at("alert_id"), "alert");
    const auto& period = result.at("periods").at(0);
    EXPECT_EQ(period.at("transform"), "h264_masked_selection");
    EXPECT_EQ(period.at("frame_count"), 25);
    EXPECT_EQ(period.at("masks").at(0).at("right"), 1);
    EXPECT_EQ(period.at("audio").at("file"), "audio.wav");
    EXPECT_EQ(period.at("audio").at("sha256"), "audio-digest");
    EXPECT_EQ(period.at("audio").at("sample_rate"), domain::ArchiveSound::sample_rate);
}
TEST(ExportCapacity, EnforcesTheConcurrentLimitAndReleasesSlots)
{
    const ExportSlot first;
    {
        const ExportSlot second;
        EXPECT_THAT([] { ExportSlot third; },
                    testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::rate_limited)));
    }
    EXPECT_NO_THROW(ExportSlot replacement);
}
} // namespace bacuri::vms::control
