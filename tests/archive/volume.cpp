#include "archive/volume.hpp"
#include <gtest/gtest.h>
#include <unistd.h>

#include <fstream>

namespace bacuri::vms::archive
{
namespace
{
using namespace std::chrono_literals;
struct TempVolume {
    fs::path root = fs::temp_directory_path() / uuid();
    Owner owner{uuid(), uuid()};
    TempVolume()
    {
        fs::create_directory(root);
    }
    ~TempVolume()
    {
        std::error_code error;
        fs::remove_all(root, error);
    }
};
Block example()
{
    auto receipt = domain::UtcTime{100us};
    return {{uuid(), uuid(), uuid(), uuid(), 1}, domain::ProjectedTime::from_reception(receipt), receipt + 1s};
}
void write_sample(const fs::path& path)
{
    std::ofstream file(path, std::ios::binary);
    const auto box = [](const char* type, const std::string& contents = {}) {
        std::string header(4, '\0');
        header.back() = static_cast<char>(8 + contents.size());
        return header + type + contents;
    };
    file << box("ftyp") << box("moov", box("trak") + box("mvex")) << box("moof", box("traf"))
         << box("mdat", "sample") << box("mfra", box("tfra") + box("mfro"));
}
class PublicationDeathTest : public testing::TestWithParam<CommitStage>
{
};

TEST_P(PublicationDeathTest, RestartImportsOnlyPublishedDirectories)
{
    TempVolume fixture;
    auto stage = GetParam();
    auto child = [&] {
        Volume volume{fixture.root, fixture.owner};
        volume.publish(example(), write_sample, [stage](CommitStage current) {
            if (current == stage) {
                _exit(42);
            }
        });
    };
    EXPECT_EXIT(child(), testing::ExitedWithCode(42), "");
    Volume restarted{fixture.root, fixture.owner};
    auto item = restarted.pending();
    const bool published =
        stage == CommitStage::renamed || stage == CommitStage::parent_synced || stage == CommitStage::journaled;
    ASSERT_EQ(item.has_value(), published);
    if (item) {
        EXPECT_TRUE(item->unavailable.empty());
        restarted.acknowledge(*item);
        EXPECT_FALSE(restarted.pending());
    }
}
INSTANTIATE_TEST_SUITE_P(DurableBoundaries, PublicationDeathTest,
                         testing::Values(CommitStage::media_synced, CommitStage::manifest_synced,
                                         CommitStage::directory_synced, CommitStage::renamed,
                                         CommitStage::parent_synced, CommitStage::journaled));

TEST(ArchiveVolume, PreservesConfirmedGapsAndReportsOnlyPendingPointCoverage)
{
    TempVolume fixture;
    auto block = example();
    Gap gap{block.origin.point, 1, 10, 50, "source_offline"};
    {
        Volume writer{fixture.root, fixture.owner};
        writer.report_gap(gap);
        writer.report_gap(gap);
        writer.publish(block, write_sample);
    }
    Volume restored{fixture.root, fixture.owner};
    auto pending = restored.pending_gap();
    if (!pending) {
        FAIL() << "Missing durable gap";
    }
    EXPECT_EQ(pending->second.point, gap.point);
    restored.acknowledge_gap(pending->first);
    EXPECT_FALSE(restored.pending_gap());
    EXPECT_TRUE(restored.pending_intervals(uuid(), 1, 10000).empty());
    ASSERT_EQ(restored.pending_intervals(block.origin.point, 1, 10000).size(), 1u);
    EXPECT_EQ(restored.pending_intervals(block.origin.point, 1, 10000, block.origin.binding).size(), 1u);
    EXPECT_TRUE(restored.pending_intervals(block.origin.point, 1, 10000, block.origin.binding + 1).empty());
    auto item = restored.pending();
    if (!item) {
        FAIL() << "Missing pending block";
    }
    restored.acknowledge(*item);
    EXPECT_TRUE(restored.pending_intervals(block.origin.point, 1, 10000).empty());
}

TEST(ArchiveVolume, RejectsSecondWriterAndDifferentAssignment)
{
    TempVolume fixture;
    {
        Volume writer{fixture.root, fixture.owner};
        EXPECT_THROW((Volume{fixture.root, fixture.owner}), std::system_error);
    }
    EXPECT_THROW((Volume{fixture.root, {fixture.owner.site, uuid()}}), std::runtime_error);
}
TEST(ArchiveVolume, FailedPublicationPreservesPreviousBlockAndResumes)
{
    TempVolume fixture;
    Volume volume{fixture.root, fixture.owner};
    volume.publish(example(), write_sample);
    EXPECT_THROW(
        volume.publish(example(),
                       [](const fs::path&) { throw std::system_error(ENOSPC, std::generic_category()); }),
        std::system_error);
    EXPECT_THROW(volume.publish(example(), [](const fs::path& path) { std::ofstream{path} << "incomplete MP4"; }),
                 std::invalid_argument);
    auto previous = volume.pending();
    ASSERT_TRUE(previous.has_value());
    if (previous) {
        volume.acknowledge(*previous);
    }
    EXPECT_FALSE(volume.pending());
    volume.publish(example(), write_sample);
    EXPECT_TRUE(volume.pending());
}

TEST(ArchiveManifest, ValidatesTimeAndIdentityBeforePublication)
{
    const Identity identity{{uuid(), uuid()}, uuid(), 1};
    auto manifest = make_manifest(identity, uuid(), example(), {128, std::string(64, 'a')});
    EXPECT_NO_THROW(validate_manifest(manifest));
    manifest["time"]["received_at_utc_us"] = 900;
    EXPECT_THROW(validate_manifest(manifest), std::invalid_argument);
    manifest["time"]["received_at_utc_us"] = manifest["begin_utc_us"];
    manifest["media"]["file"] = "../media.mp4";
    EXPECT_THROW(validate_manifest(manifest), std::invalid_argument);
}
} // namespace
} // namespace bacuri::vms::archive
