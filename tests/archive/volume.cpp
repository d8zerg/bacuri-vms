#include "archive/volume.hpp"
#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>

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
    TempVolume(const TempVolume&) = delete;
    TempVolume& operator=(const TempVolume&) = delete;
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
    auto item = restarted.journal.pending();
    const bool published =
        stage == CommitStage::renamed || stage == CommitStage::parent_synced || stage == CommitStage::journaled;
    ASSERT_EQ(item.has_value(), published);
    if (item) {
        EXPECT_TRUE(item->unavailable.empty());
        restarted.journal.acknowledge(*item);
        EXPECT_FALSE(restarted.journal.pending());
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
        auto old = volume.journal.pending();
        if (!old)
            FAIL() << "Expected the first published block";
        expired_directory = old->directory;
        volume.journal.acknowledge(*old);
        volume.publish(protected_block, write_sample);
        auto remaining = volume.journal.pending();
        if (!remaining)
            FAIL() << "Expected the second published block";
        protected_directory = remaining->directory;
    }
    const auto stage = GetParam();
    const auto cutoff = expired.end.time_since_epoch().count();
    auto interrupt = [&] {
        Volume volume{fixture.root, fixture.owner};
        volume.retention.apply_retention({.cutoff_us = cutoff, .days = 1, .reserve_percent = 20},
                                         [&](RetirementStage current) {
                                             if (current == stage)
                                                 _exit(42);
                                         });
    };
    EXPECT_EXIT(interrupt(), testing::ExitedWithCode(42), "");
    Volume restarted{fixture.root, fixture.owner};
    restarted.retention.apply_retention({.cutoff_us = cutoff - 1, .days = 2, .reserve_percent = 20});
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
        writer.journal.report_gap(gap);
        writer.journal.report_gap(gap);
        writer.publish(block, write_sample);
    }
    Volume restored{fixture.root, fixture.owner};
    auto pending = restored.journal.pending_gap();
    if (!pending) {
        FAIL() << "Missing durable gap";
    }
    EXPECT_EQ(pending->second.point, gap.point);
    restored.journal.acknowledge_gap(pending->first);
    EXPECT_FALSE(restored.journal.pending_gap());
    EXPECT_TRUE(restored.journal.pending_intervals(uuid(), 1, 10000).empty());
    ASSERT_EQ(restored.journal.pending_intervals(block.origin.point, 1, 10000).size(), 1u);
    EXPECT_EQ(restored.journal.pending_intervals(block.origin.point, 1, 10000, block.origin.binding).size(), 1u);
    EXPECT_TRUE(
        restored.journal.pending_intervals(block.origin.point, 1, 10000, block.origin.binding + 1).empty());
    const auto end = block.end.time_since_epoch().count();
    EXPECT_TRUE(restored.journal.pending_intervals(block.origin.point, 1, 10000, {}, end).empty());
    EXPECT_EQ(restored.journal.pending_intervals(block.origin.point, 1, 10000, {}, end - 1).size(), 1u);
    auto item = restored.journal.pending();
    if (!item) {
        FAIL() << "Missing pending block";
    }
    restored.journal.acknowledge(*item);
    EXPECT_TRUE(restored.journal.pending_intervals(block.origin.point, 1, 10000).empty());
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
    auto previous = volume.journal.pending();
    ASSERT_TRUE(previous.has_value());
    if (previous) {
        volume.journal.acknowledge(*previous);
    }
    EXPECT_FALSE(volume.journal.pending());
    volume.publish(example(), write_sample);
    EXPECT_TRUE(volume.journal.pending());
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
        auto pending = volume.journal.pending();
        if (!pending)
            FAIL() << "Expected a published block with audio";
        const auto& manifest = pending->manifest;
        id = manifest.recording.block().id.to_string();
        if (!manifest.audio)
            FAIL() << "Expected recorded audio metadata";
        hash = digest_hex(manifest.audio->sha256);
        EXPECT_EQ(volume.read_audio({id, hash, 20, 100}), pcm.substr(20, 100));
        EXPECT_THROW(volume.read_audio({id, std::string(64, '0'), 20, 100}), std::invalid_argument);
        EXPECT_EQ(archive_status(fixture.root).at("media_bytes").get<std::size_t>(),
                  static_cast<std::size_t>(manifest.size_bytes) + pcm.size());
    }
    ASSERT_TRUE(fs::remove(fixture.root / "blocks" / id / "audio.pcm"));
    Volume restarted{fixture.root, fixture.owner};
    restarted.verification.verify_all();
    EXPECT_EQ(archive_status(fixture.root).at("corrupt"), 1);
}

// Publishes one block, acknowledges its catalog registration and returns its directory.
std::string committed_block(Volume& volume)
{
    volume.publish(example(), write_sample);
    auto item = volume.journal.pending();
    if (!item)
        throw std::runtime_error("Expected a published block");
    volume.journal.acknowledge(*item);
    return item->directory;
}

void flip_last_byte(const fs::path& path)
{
    std::fstream media{path, std::ios::in | std::ios::out | std::ios::binary};
    media.seekg(-1, std::ios::end);
    const auto byte = static_cast<char>(media.get() ^ 1);
    media.seekp(-1, std::ios::end);
    media.put(byte);
}

TEST(ArchiveVerification, OpeningDefersMediaChecksumsToBackgroundVerification)
{
    TempVolume fixture;
    std::string directory;
    {
        Volume volume{fixture.root, fixture.owner};
        directory = committed_block(volume);
        EXPECT_FALSE(volume.verification.verify_next()) << "A published block is verified when it is journaled";
    }
    flip_last_byte(fixture.root / directory / "media.mp4");
    Volume reopened{fixture.root, fixture.owner};
    EXPECT_EQ(archive_status(fixture.root).at("corrupt"), 0);
    EXPECT_FALSE(reopened.journal.pending());

    const auto result = reopened.verification.verify_next();
    if (!result)
        FAIL() << "Expected a block due for verification";
    EXPECT_FALSE(result->available);
    EXPECT_GT(result->bytes_read, 0u);
    EXPECT_EQ(archive_status(fixture.root).at("corrupt"), 1);
    const auto changed = reopened.journal.pending();
    if (!changed)
        FAIL() << "Expected a changed verdict to register";
    EXPECT_EQ(changed->unavailable, "corrupt");
    EXPECT_FALSE(reopened.verification.verify_next())
        << "A verified block is due only after the next open or interval";
}

TEST(ArchiveVerification, OpeningMarksMissingDirectoriesAndVerificationRestoresThem)
{
    TempVolume fixture;
    std::string directory;
    {
        Volume volume{fixture.root, fixture.owner};
        directory = committed_block(volume);
    }
    const auto detached = fixture.root / "detached";
    fs::rename(fixture.root / directory, detached);
    {
        Volume reopened{fixture.root, fixture.owner};
        EXPECT_EQ(archive_status(fixture.root).at("missing"), 1);
        auto missing = reopened.journal.pending();
        if (!missing)
            FAIL() << "Expected a missing block to register";
        EXPECT_EQ(missing->unavailable, "missing");
        reopened.journal.acknowledge(*missing);
    }
    fs::rename(detached, fixture.root / directory);
    Volume repaired{fixture.root, fixture.owner};
    EXPECT_EQ(archive_status(fixture.root).at("missing"), 1) << "Only verified media becomes available";
    const auto result = repaired.verification.verify_next();
    if (!result)
        FAIL() << "Expected the missing block to be due for verification";
    EXPECT_TRUE(result->available);
    EXPECT_EQ(archive_status(fixture.root).at("missing"), 0);
    const auto available = repaired.journal.pending();
    if (!available)
        FAIL() << "Expected the restored block to register";
    EXPECT_TRUE(available->unavailable.empty());
}

TEST(ArchiveVerification, EachOpenVerifiesEveryBlockOnce)
{
    TempVolume fixture;
    {
        Volume volume{fixture.root, fixture.owner};
        committed_block(volume);
        committed_block(volume);
    }
    Volume reopened{fixture.root, fixture.owner};
    std::set<std::string> verified;
    while (const auto result = reopened.verification.verify_next()) {
        EXPECT_TRUE(result->available);
        EXPECT_TRUE(verified.insert(result->block_id).second);
    }
    EXPECT_EQ(verified.size(), 2u);
}

TEST(ArchiveVerification, OpeningRemovesInterruptedStagingDirectories)
{
    TempVolume fixture;
    {
        Volume volume{fixture.root, fixture.owner};
    }
    const auto staging = fixture.root / "blocks" / (".partial-" + uuid());
    fs::create_directory(staging);
    write_sample(staging / "media.mp4");
    Volume reopened{fixture.root, fixture.owner};
    EXPECT_FALSE(fs::exists(staging));
    EXPECT_FALSE(reopened.journal.pending());
}

TEST(ArchiveVerification, RepairsCorruptPrimaryFromVerifiedCopy)
{
    TempVolume fixture;
    const fs::path mount{fixture.root.string() + "-replica"};
    fs::create_directory(mount);
    const VolumeOptions copies_configured{.replica = mount, .invalid = {}};
    std::string directory;
    {
        Volume volume{fixture.root, fixture.owner, copies_configured};
        directory = committed_block(volume);
    }
    const auto copies = mount / ("bacuri-" + fixture.owner.site + "-" + fixture.owner.node);
    fs::create_directories(copies);
    const auto copy = copies / fs::path{directory}.filename();
    fs::copy(fixture.root / directory, copy);
    std::ifstream original_file{copy / "media.mp4", std::ios::binary};
    const std::string original{std::istreambuf_iterator<char>{original_file}, {}};
    flip_last_byte(fixture.root / directory / "media.mp4");

    Volume reopened{fixture.root, fixture.owner, copies_configured};
    const auto result = reopened.verification.verify_next();
    if (!result)
        FAIL() << "Expected the corrupt block to be due for verification";
    EXPECT_TRUE(result->available);
    std::ifstream repaired_file{fixture.root / directory / "media.mp4", std::ios::binary};
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>{repaired_file}, {}), original);
    EXPECT_EQ(archive_status(fixture.root).at("corrupt"), 0);
    std::error_code ignored;
    fs::remove_all(mount, ignored);
}

TEST(ArchiveVerification, RetiredBlocksAreRemovedFromBothCopies)
{
    TempVolume fixture, replica;
    const VolumeOptions options{.replica = replica.root, .invalid = {}};
    Volume volume{fixture.root, fixture.owner, options};
    auto block = example();
    volume.publish(block, write_sample);
    auto item = volume.journal.pending();
    if (!item)
        FAIL() << "Expected a published block awaiting registration";
    volume.journal.acknowledge(*item);
    ASSERT_TRUE(volume.verification.scrub_copy());
    const auto copy = replica.root / ("bacuri-" + fixture.owner.site + "-" + fixture.owner.node) /
                      item->manifest.recording.block().id.to_string();
    ASSERT_TRUE(fs::exists(copy / "media.mp4"));
    volume.retention.apply_retention({.cutoff_us = item->manifest.end_us(), .days = 1, .reserve_percent = 20});
    ASSERT_TRUE(volume.verification.scrub_copy());
    EXPECT_FALSE(fs::exists(copy));
    EXPECT_FALSE(fs::exists(fixture.root / item->directory));
    EXPECT_FALSE(volume.verification.verify_next());
}

TEST(ArchiveManifest, PreservesSourceClockTracksAndAudioAcrossSerialization)
{
    auto block = example();
    const auto at = block.first.utc();
    const domain::SourceTime source{321us};
    const auto epoch = domain::ClockEpochId::parse(uuid());
    if (!epoch)
        FAIL() << "Expected a valid clock epoch";
    const domain::ClockAnchor anchor{*epoch, source, at, 12us};
    block.first = domain::ProjectedTime::from_source(anchor, source, at + 30us).value();
    block.edge_recording = std::string(64, 'b');
    const Identity identity{{uuid(), uuid()}, uuid(), 9};
    auto manifest = make_manifest(identity, uuid(), block, {500, std::string(64, 'a')});
    manifest.audio = domain::ManifestAudio{manifest.begin_us(), 16, parse_digest(std::string(64, 'c'))};
    manifest.tracks.push_back({2, domain::TrackKind::audio, "pcm_s16le", 8000});
    auto wire = manifest_json(manifest);
    auto parsed = parse_manifest(wire);
    EXPECT_EQ(manifest_json(parsed), wire);
    EXPECT_EQ(parsed.recording.first_frame().source(), source);
    EXPECT_EQ(parsed.recording.first_frame().uncertainty(), 12us);
    EXPECT_EQ(parsed.edge_recording, block.edge_recording);
    EXPECT_EQ(parsed.tracks.back().kind, domain::TrackKind::audio);
    wire["audio"]["size_bytes"] = 256000;
    EXPECT_THROW(parse_manifest(wire), std::invalid_argument);
    wire.erase("audio");
    wire["unexpected"] = true;
    EXPECT_THROW(parse_manifest(wire), std::invalid_argument);
    EXPECT_THROW(make_manifest(identity, uuid(), block, (MediaDigest{1, std::string(64, 'A')})),
                 std::invalid_argument);
}

TEST(ArchiveManifest, ValidatesTimeAndIdentityBeforePublication)
{
    const Identity identity{{uuid(), uuid()}, uuid(), 1};
    auto manifest = manifest_json(make_manifest(identity, uuid(), example(), {128, std::string(64, 'a')}));
    EXPECT_EQ(manifest_json(parse_manifest(manifest)), manifest);
    EXPECT_NO_THROW(parse_manifest(manifest));
    manifest["time"]["received_at_utc_us"] = 900;
    EXPECT_THROW(parse_manifest(manifest), std::invalid_argument);
    manifest["time"]["received_at_utc_us"] = manifest["begin_utc_us"];
    manifest["media"]["file"] = "../media.mp4";
    EXPECT_THROW(parse_manifest(manifest), std::invalid_argument);
}
} // namespace
} // namespace bacuri::vms::archive
