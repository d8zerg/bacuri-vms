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

class RetirementDeathTest : public testing::TestWithParam<RetirementStage>
{
};

TEST_P(RetirementDeathTest, RestartCompletesOnlyAuthorizedRemoval)
{
    TempVolume fixture;
    auto expired = example(), protected_block = expired;
    protected_block.end += 1us;
    std::string expired_directory, protected_directory;
    {
        Volume volume{fixture.root, fixture.owner};
        volume.publish(expired, write_sample);
        auto old = volume.pending();
        if (!old)
            FAIL() << "Expected the first published block";
        expired_directory = old->directory;
        volume.acknowledge(*old);
        volume.publish(protected_block, write_sample);
        auto remaining = volume.pending();
        if (!remaining)
            FAIL() << "Expected the second published block";
        protected_directory = remaining->directory;
    }
    const auto stage = GetParam();
    const auto cutoff = expired.end.time_since_epoch().count();
    auto interrupt = [&] {
        Volume volume{fixture.root, fixture.owner};
        volume.apply_retention({.cutoff_us = cutoff, .days = 1, .reserve_percent = 20},
                               [&](RetirementStage current) {
                                   if (current == stage)
                                       _exit(42);
                               });
    };
    EXPECT_EXIT(interrupt(), testing::ExitedWithCode(42), "");
    Volume restarted{fixture.root, fixture.owner};
    restarted.apply_retention({.cutoff_us = cutoff - 1, .days = 2, .reserve_percent = 20});
    EXPECT_FALSE(fs::exists(fixture.root / expired_directory));
    EXPECT_TRUE(fs::is_regular_file(fixture.root / protected_directory / "media.mp4"));
    const auto status = archive_status(fixture.root);
    EXPECT_EQ(status.at("cutoff_utc_us"), cutoff);
    EXPECT_EQ(status.at("removed_blocks"), 1);
    EXPECT_EQ(status.at("committed"), 1);
    EXPECT_EQ(status.at("missing"), 0);
}
INSTANTIATE_TEST_SUITE_P(DurableBoundaries, RetirementDeathTest,
                         testing::Values(RetirementStage::authorized, RetirementStage::renamed,
                                         RetirementStage::removed));

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
    const auto end = block.end.time_since_epoch().count();
    EXPECT_TRUE(restored.pending_intervals(block.origin.point, 1, 10000, {}, end).empty());
    EXPECT_EQ(restored.pending_intervals(block.origin.point, 1, 10000, {}, end - 1).size(), 1u);
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
    EXPECT_THROW(volume.publish(example(),
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

TEST(ArchiveVolume, ValidatesRecordedSoundOnReadAndRestart)
{
    TempVolume fixture;
    auto block = example();
    std::string pcm(320, 'x');
    std::string id, hash;
    {
        Volume volume{fixture.root, fixture.owner};
        volume.publish(block, write_sample, {}, 0, {100, pcm});
        auto pending = volume.pending();
        if (!pending)
            FAIL() << "Expected a published block with audio";
        auto manifest = Json::parse(pending->manifest);
        id = manifest.at("block_id");
        hash = manifest.at("audio").at("sha256");
        EXPECT_EQ(volume.read_audio({id, hash, 20, 100}), pcm.substr(20, 100));
        EXPECT_THROW(volume.read_audio({id, std::string(64, '0'), 20, 100}), std::invalid_argument);
        EXPECT_EQ(archive_status(fixture.root).at("media_bytes").get<std::size_t>(),
                  manifest["media"]["size_bytes"].get<std::size_t>() + pcm.size());
    }
    ASSERT_TRUE(fs::remove(fixture.root / "blocks" / id / "audio.pcm"));
    Volume restarted{fixture.root, fixture.owner};
    EXPECT_EQ(archive_status(fixture.root).at("corrupt"), 1);
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
