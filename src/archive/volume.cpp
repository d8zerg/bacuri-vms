#include "archive/volume.hpp"
#include "archive/volume_io.hpp"
#include "archive/mp4.hpp"
#include "archive/replica.hpp"
#include "platform/identifiers.hpp"
#include <sys/file.h>
#include <sys/stat.h>
#include <fstream>

namespace bacuri::vms::archive
{
namespace
{
MediaDigest digest_file(const fs::path& path)
{
    File file{path, O_RDONLY | O_NONBLOCK};
    struct stat info{};
    system_check(fstat(file.fd, &info));
    if (!S_ISREG(info.st_mode) || info.st_size <= 0) {
        throw std::invalid_argument("Invalid archive media file");
    }
    verify_fragment_layout(file.fd, static_cast<std::uint64_t>(info.st_size));
    return digest_media(file.fd);
}

BlockManifest read_manifest(const fs::path& path)
{
    if (!fs::is_regular_file(fs::symlink_status(path)) || fs::file_size(path) > manifest_limit) {
        throw std::invalid_argument("Invalid manifest file");
    }
    std::ifstream file(path, std::ios::binary);
    file.exceptions(std::ios::badbit | std::ios::failbit);
    return parse_manifest(Json::parse(file));
}

} // namespace
Volume::Lock::Lock(const fs::path& root) : fd{open(root.c_str(), O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_CLOEXEC)}
{
    system_check(fd);
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        const auto error = errno;
        close(fd);
        throw std::system_error(error, std::generic_category(), "Archive volume already owned or unavailable");
    }
}
Volume::Lock::~Lock()
{
    close(fd);
}

Volume::Volume(const fs::path& root, Owner owner, VolumeOptions options)
    : root_{root}, replica_{std::move(options.replica)}, lock_{root}, journal{root_, std::move(owner)},
      retention{journal, replica_mutex_, lock_.fd, replica_.has_value()},
      verification{journal, replica_mutex_, replica_}
{
    const auto& invalid = options.invalid;
    const bool assigned = journal.assign_owner();
    fs::create_directory(root_ / "blocks");
    if (!fs::is_directory(fs::symlink_status(root_ / "blocks"))) {
        throw std::runtime_error("Invalid archive directory");
    }
    sync_path(root_, O_DIRECTORY);
    File block_directory{root_ / "blocks", O_RDONLY | O_DIRECTORY};
    system_check(flock(block_directory.fd, LOCK_EX));
    if (!invalid)
        retention.reap({});
    reconcile(invalid);
    journal.finish_open(static_cast<bool>(invalid), assigned);
}
bool Volume::publish(const Block& block, const MediaWriter& write_media, const Observer& observer,
                     std::uintmax_t expected_bytes, RecordedSound sound)
{
    const auto [audio_start, pcm] = sound;
    const auto id = uuid();
    const auto blocks = root_ / "blocks";
    const auto temp = blocks / (".partial-" + id);
    {
        std::scoped_lock guard{journal.mutex_};
        retention.require_space(expected_bytes + pcm.size(), 0);
    }
    if (!fs::create_directory(temp)) {
        throw std::runtime_error("Duplicate archive staging directory");
    }
    try {
        const auto media = temp / "media.mp4";
        write_media(media);
        sync_path(media, O_RDONLY);
        if (observer)
            observer(CommitStage::media_synced);
        auto manifest = make_manifest(journal.identity_, id, block, digest_file(media));
        if (!pcm.empty()) {
            File audio{temp / "audio.pcm", O_RDWR | O_CREAT | O_EXCL};
            std::size_t offset = 0;
            while (offset < pcm.size()) {
                auto count = write(audio.fd, pcm.data() + offset, pcm.size() - offset);
                if (count < 0 && errno == EINTR)
                    continue;
                if (count <= 0)
                    throw std::runtime_error("Cannot store archive sound");
                offset += static_cast<std::size_t>(count);
            }
            system_check(fsync(audio.fd));
            if (lseek(audio.fd, 0, SEEK_SET) < 0)
                system_check(-1);
            auto digest = digest_media(audio.fd);
            manifest.audio = domain::ManifestAudio{audio_start, digest.bytes, parse_digest(digest.sha256)};
        }
        const auto manifest_path = temp / "manifest.json";
        {
            std::ofstream file;
            file.exceptions(std::ios::badbit | std::ios::failbit);
            file.open(manifest_path, std::ios::binary);
            file << manifest_json(manifest).dump();
            file.close();
        }
        sync_path(manifest_path, O_RDONLY);
        if (observer)
            observer(CommitStage::manifest_synced);
        sync_path(temp, O_DIRECTORY);
        if (observer)
            observer(CommitStage::directory_synced);
        auto relative = "blocks/" + id;
        std::scoped_lock guard{journal.mutex_};
        journal.journal_published();
        if (!block.edge_recording.empty() && journal.overlaps(block)) {
            fs::remove_all(temp);
            return false;
        }
        retention.require_space(0, static_cast<std::uintmax_t>(manifest.size_bytes) + pcm.size());
        system_check(renameat2(AT_FDCWD, temp.c_str(), AT_FDCWD, (blocks / id).c_str(), RENAME_NOREPLACE));
        journal.published_.emplace(std::move(relative), std::move(manifest));
        if (observer)
            observer(CommitStage::renamed);
        journal.journal_published(observer);
        return true;
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(temp, ignored);
        throw;
    }
}

std::string Volume::read_audio(const AudioSlice& slice)
{
    if (!platform::canonical_id(slice.block) || slice.count > 16000 || slice.offset > 256000)
        throw std::invalid_argument("Invalid audio interval");
    auto read = [&](const fs::path& base) {
        File dir{base / slice.block, O_RDONLY | O_DIRECTORY};
        File file{openat(dir.fd, "audio.pcm", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW)};
        struct stat info{};
        system_check(fstat(file.fd, &info));
        if (!S_ISREG(info.st_mode) || info.st_size <= 0 || info.st_size > 256000)
            throw std::invalid_argument("Invalid archive audio file");
        auto digest = digest_media(file.fd);
        if (digest.sha256 != slice.hash || slice.offset + slice.count > static_cast<std::size_t>(digest.bytes))
            throw std::invalid_argument("Invalid audio content");
        std::string pcm(slice.count, '\0');
        if (pread(file.fd, pcm.data(), slice.count, static_cast<off_t>(slice.offset)) !=
            static_cast<ssize_t>(slice.count))
            throw std::runtime_error("Archive audio unavailable");
        return pcm;
    };
    try {
        return read(root_ / "blocks");
    } catch (const std::exception&) {
        auto copies = replica_blocks(replica_, root_, owner());
        if (!copies)
            throw;
        return read(*copies);
    }
}

void Volume::recover_directory(const fs::directory_entry& entry, const InvalidBlock& invalid)
{
    const auto id = entry.path().filename().string();
    if (!platform::canonical_id(id)) {
        return;
    }
    const auto relative = "blocks/" + id;
    // Background verification checks the media of journaled blocks.
    if (journal.contains(relative))
        return;
    // A directory published just before a crash, or every directory after the journal was rebuilt.
    std::optional<BlockManifest> verified;
    try {
        if (!fs::is_directory(entry.symlink_status())) {
            throw std::invalid_argument("Invalid block directory");
        }
        auto m = read_manifest(entry.path() / "manifest.json");
        if (m.recording.block().id.to_string() != id ||
            m.recording.block().site.to_string() != journal.identity_.owner.site ||
            m.recording_node.to_string() != journal.identity_.owner.node) {
            throw std::invalid_argument("Archive identity mismatch");
        }
        const auto media = entry.path() / "media.mp4";
        if (!fs::is_regular_file(fs::symlink_status(media))) {
            throw std::invalid_argument("Invalid archive media");
        }
        auto digest = digest_file(media);
        if (m.size_bytes != digest.bytes || digest_hex(m.sha256) != digest.sha256) {
            throw std::invalid_argument("Archive checksum mismatch");
        }
        if (m.audio) {
            const auto path = entry.path() / "audio.pcm";
            if (!fs::is_regular_file(fs::symlink_status(path)) ||
                fs::file_size(path) != static_cast<std::uintmax_t>(m.audio->size_bytes))
                throw std::invalid_argument("Invalid archive audio");
            File audio{path, O_RDONLY | O_NONBLOCK};
            const auto audio_digest = digest_media(audio.fd);
            if (m.audio->size_bytes != audio_digest.bytes || digest_hex(m.audio->sha256) != audio_digest.sha256)
                throw std::invalid_argument("Archive audio checksum mismatch");
        }
        verified = std::move(m);
    } catch (const Json::exception&) {
        std::fprintf(stderr, "archive corrupt manifest %s\n", id.c_str());
    } catch (const std::invalid_argument& error) {
        std::fprintf(stderr, "archive corrupt %s: %s\n", id.c_str(), error.what());
    } catch (const std::system_error& error) {
        std::fprintf(stderr, "archive corrupt %s: %s\n", id.c_str(), error.what());
    }
    if (verified) {
        journal.record(relative, *verified);
    } else if (invalid) {
        invalid(id);
    }
}

void Volume::reconcile(const InvalidBlock& invalid)
{
    constexpr std::string_view expired_prefix = ".expired-", staging_prefix = ".partial-";
    File directory{openat(lock_.fd, "blocks", O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW | O_RDONLY)};
    system_check(fsync(directory.fd));
    for (const auto& entry : fs::directory_iterator(root_ / "blocks")) {
        const auto name = entry.path().filename().string();
        // The durable rename also records the decision if the local journal is rebuilt.
        if (name.starts_with(expired_prefix) &&
            platform::canonical_id(std::string_view{name}.substr(expired_prefix.size()))) {
            finish_removal(directory.fd, name);
            system_check(fsync(directory.fd));
        } else if (name.starts_with(staging_prefix) &&
                   platform::canonical_id(std::string_view{name}.substr(staging_prefix.size()))) {
            // An interrupted publication never reached the rename, so nothing refers to its staging directory.
            std::error_code error;
            fs::remove_all(entry.path(), error);
            std::fprintf(stderr, "archive incomplete %s %s\n", name.c_str(), error ? "retained" : "removed");
        } else {
            recover_directory(entry, invalid);
        }
    }
    journal.mark_missing_directories();
}

} // namespace bacuri::vms::archive
