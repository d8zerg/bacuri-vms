#include "archive/volume.hpp"
#include "archive/mp4.hpp"
#include "archive_sql.hpp"
#include "control/support.hpp"

#include <fcntl.h>
#include <sodium.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <system_error>
#include <utility>

namespace bacuri::vms::archive
{
namespace
{
using storage::bind_text;
using storage::column;
using storage::next;

void system_check(int result)
{
    if (result < 0) {
        throw std::system_error(errno, std::generic_category(), "Archive volume I/O");
    }
}

class File
{
  public:
    File(const fs::path& path, int flags) : fd{open(path.c_str(), flags | O_CLOEXEC | O_NOFOLLOW, 0600)}
    {
        system_check(fd);
    }
    ~File()
    {
        close(fd);
    }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    int fd;
};

void sync_path(const fs::path& path, int flags)
{
    File file{path, flags};
    system_check(fsync(file.fd));
}

MediaDigest digest_file(const fs::path& path)
{
    File file{path, O_RDONLY | O_NONBLOCK};
    struct stat info{};
    system_check(fstat(file.fd, &info));
    if (!S_ISREG(info.st_mode) || info.st_size <= 0) {
        throw std::invalid_argument("Invalid archive media file");
    }
    verify_fragment_layout(file.fd, static_cast<std::uint64_t>(info.st_size));
    crypto_hash_sha256_state state{};
    crypto_hash_sha256_init(&state);
    std::array<unsigned char, 64uz * 1024> buffer;
    std::int64_t total{};
    for (;;) {
        auto count = read(file.fd, buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            throw std::system_error(errno, std::generic_category(), "Cannot read archive media");
        }
        if (count == 0) {
            break;
        }
        crypto_hash_sha256_update(&state, buffer.data(), count);
        total += count;
    }
    std::array<unsigned char, crypto_hash_sha256_BYTES> hash{};
    std::array<char, crypto_hash_sha256_BYTES * 2 + 1> hex{};
    crypto_hash_sha256_final(&state, hash.data());
    sodium_bin2hex(hex.data(), hex.size(), hash.data(), hash.size());
    return {total, hex.data()};
}

Json read_manifest(const fs::path& path)
{
    if (!fs::is_regular_file(fs::symlink_status(path)) || fs::file_size(path) > manifest_limit) {
        throw std::invalid_argument("Invalid manifest file");
    }
    std::ifstream file(path, std::ios::binary);
    file.exceptions(std::ios::badbit | std::ios::failbit);
    return Json::parse(file);
}

auto now_us()
{
    return std::chrono::time_point_cast<domain::Microseconds>(std::chrono::system_clock::now())
        .time_since_epoch()
        .count();
}

void notify(const Volume::Observer& observer, CommitStage stage)
{
    if (observer) {
        observer(stage);
    }
}
} // namespace

Json archive_status(const fs::path& root)
{
    storage::Sqlite db{root / "journal.db", storage::OpenMode::read_only};
    auto row = db.query(
        "SELECT json_object('committed',count(*),'pending',count(*) FILTER "
        "(WHERE registered_at_utc_us IS NULL AND catalog_error IS NULL),"
        "'missing',count(*) FILTER (WHERE unavailable_reason='missing'),"
        "'corrupt',count(*) FILTER (WHERE unavailable_reason='corrupt'),"
        "'rejected',count(catalog_error),'last_commit_utc_us',max(committed_at_utc_us)) FROM committed_blocks");
    next(row.get());
    return Json::parse(column(row.get(), 0));
}

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

Volume::Volume(const fs::path& root, Owner owner)
    : root_{root}, lock_{root}, db_{root / "journal.db"}, identity_{std::move(owner), {}, 0}
{
    int version{};
    {
        auto version_query = db_.query("PRAGMA user_version");
        next(version_query.get());
        version = sqlite3_column_int(version_query.get(), 0);
    }
    if (version < 0 || version > 2) {
        throw std::runtime_error("Unsupported archive journal version");
    }
    if (version == 0) {
        db_.exec(sql::initial);
    }
    if (version < 2) {
        db_.exec(sql::upgrade);
    }
    auto assign = db_.query("INSERT INTO volume_owner VALUES(1,?,?,?,1) ON CONFLICT(singleton) DO UPDATE SET "
                            "generation=generation+1 WHERE site_id=excluded.site_id AND node_id=excluded.node_id "
                            "RETURNING epoch,generation");
    bind_text(assign.get(), 1, identity_.owner.site);
    bind_text(assign.get(), 2, identity_.owner.node);
    bind_text(assign.get(), 3, uuid());
    if (!next(assign.get())) {
        throw std::runtime_error("Archive belongs to another site or node");
    }
    identity_.epoch = column(assign.get(), 0);
    identity_.generation = sqlite3_column_int64(assign.get(), 1);
    next(assign.get());
    fs::create_directory(root_ / "blocks");
    if (!fs::is_directory(fs::symlink_status(root_ / "blocks"))) {
        throw std::runtime_error("Invalid archive directory");
    }
    sync_path(root_, O_DIRECTORY);
    reconcile();
}

void Volume::record(const std::string& relative, const Json& m)
{
    auto save = db_.query("INSERT INTO committed_blocks(site_id,block_id,recording_epoch_id,owner_generation,"
                          "manifest_json,relative_directory,committed_at_utc_us) VALUES(?,?,?,?,?,?,?)");
    bind_text(save.get(), 1, identity_.owner.site);
    bind_text(save.get(), 2, m.at("block_id"));
    bind_text(save.get(), 3, m.at("recording_epoch_id"));
    sqlite3_bind_int64(save.get(), 4, m.at("owner_generation"));
    bind_text(save.get(), 5, m.dump());
    bind_text(save.get(), 6, relative);
    sqlite3_bind_int64(save.get(), 7, now_us());
    next(save.get());
}

void Volume::publish(const Block& block, const MediaWriter& write_media, const Observer& observer)
{
    const auto id = uuid();
    const auto blocks = root_ / "blocks";
    const auto temp = blocks / (".partial-" + id);
    if (!fs::create_directory(temp)) {
        throw std::runtime_error("Duplicate archive staging directory");
    }
    try {
        const auto media = temp / "media.mp4";
        write_media(media);
        sync_path(media, O_RDONLY);
        notify(observer, CommitStage::media_synced);
        auto manifest = make_manifest(identity_, id, block, digest_file(media));
        const auto manifest_path = temp / "manifest.json";
        {
            std::ofstream file;
            file.exceptions(std::ios::badbit | std::ios::failbit);
            file.open(manifest_path, std::ios::binary);
            file << manifest.dump();
            file.close();
        }
        sync_path(manifest_path, O_RDONLY);
        notify(observer, CommitStage::manifest_synced);
        sync_path(temp, O_DIRECTORY);
        notify(observer, CommitStage::directory_synced);
        auto relative = "blocks/" + id;
        std::scoped_lock guard{mutex_};
        journal_published();
        system_check(renameat2(AT_FDCWD, temp.c_str(), AT_FDCWD, (blocks / id).c_str(), RENAME_NOREPLACE));
        published_.emplace(std::move(relative), std::move(manifest));
        notify(observer, CommitStage::renamed);
        journal_published(observer);
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(temp, ignored);
        throw;
    }
}

void Volume::journal_published(const Observer& observer)
{
    if (published_) {
        sync_path(root_ / "blocks", O_DIRECTORY);
        notify(observer, CommitStage::parent_synced);
        record(published_->first, published_->second);
        published_.reset();
        notify(observer, CommitStage::journaled);
    }
}

void Volume::availability(const std::string& relative, const std::string& reason)
{
    auto update =
        db_.query("UPDATE committed_blocks SET unavailable_reason=nullif(?2,''),registered_at_utc_us=NULL,"
                  "catalog_error=NULL WHERE relative_directory=?1 AND unavailable_reason IS NOT nullif(?2,'')");
    bind_text(update.get(), 1, relative);
    bind_text(update.get(), 2, reason);
    next(update.get());
}

void Volume::recover_directory(const fs::directory_entry& entry)
{
    const auto id = entry.path().filename().string();
    if (!control::canonical_id(id)) {
        if (id.starts_with(".partial-")) {
            std::fprintf(stderr, "archive incomplete %s\n", id.c_str());
        }
        return;
    }
    const auto relative = "blocks/" + id;
    auto previous = db_.query("SELECT manifest_json FROM committed_blocks WHERE relative_directory=?");
    bind_text(previous.get(), 1, relative);
    std::optional<Json> expected;
    if (next(previous.get())) {
        expected = Json::parse(column(previous.get(), 0));
    }
    previous.reset();
    std::optional<Json> verified;
    try {
        if (!fs::is_directory(entry.symlink_status())) {
            throw std::invalid_argument("Invalid block directory");
        }
        auto m = read_manifest(entry.path() / "manifest.json");
        validate_manifest(m);
        if (m.at("block_id") != id || m.at("site_id") != identity_.owner.site ||
            m.at("recording_node_id") != identity_.owner.node || (expected && *expected != m)) {
            throw std::invalid_argument("Archive identity mismatch");
        }
        const auto media = entry.path() / "media.mp4";
        if (!fs::is_regular_file(fs::symlink_status(media))) {
            throw std::invalid_argument("Invalid archive media");
        }
        auto digest = digest_file(media);
        if (m.at("media").at("size_bytes") != digest.bytes || m.at("media").at("sha256") != digest.sha256) {
            throw std::invalid_argument("Archive checksum mismatch");
        }
        verified = std::move(m);
    } catch (const Json::exception&) {
        std::fprintf(stderr, "archive corrupt manifest %s\n", id.c_str());
    } catch (const std::invalid_argument& error) {
        std::fprintf(stderr, "archive corrupt %s: %s\n", id.c_str(), error.what());
    }
    if (expected) {
        availability(relative, verified ? "" : "corrupt");
    } else if (verified) {
        record(relative, *verified);
    }
}

void Volume::reconcile()
{
    sync_path(root_ / "blocks", O_DIRECTORY);
    for (const auto& entry : fs::directory_iterator(root_ / "blocks")) {
        recover_directory(entry);
    }
    auto directories = db_.query("SELECT relative_directory FROM committed_blocks ORDER BY relative_directory");
    while (next(directories.get())) {
        auto relative = column(directories.get(), 0);
        if (!fs::exists(fs::symlink_status(root_ / relative))) {
            availability(relative, "missing");
            std::fprintf(stderr, "archive missing %s\n", relative.c_str());
        }
    }
}

std::optional<Pending> Volume::pending()
{
    std::scoped_lock guard{mutex_};
    journal_published();
    auto row = db_.query(
        "SELECT relative_directory,manifest_json,coalesce(unavailable_reason,'') FROM committed_blocks "
        "WHERE registered_at_utc_us IS NULL AND catalog_error IS NULL ORDER BY committed_at_utc_us LIMIT 1");
    if (!next(row.get())) {
        return {};
    }
    return Pending{column(row.get(), 0), column(row.get(), 1), column(row.get(), 2)};
}

void Volume::acknowledge(const Pending& item, const std::string& rejection)
{
    std::scoped_lock guard{mutex_};
    auto row =
        db_.query("UPDATE committed_blocks SET registered_at_utc_us=CASE WHEN ?2='' THEN ?3 END,"
                  "catalog_error=nullif(?2,'') WHERE relative_directory=?1 AND coalesce(unavailable_reason,'')=?4 "
                  "AND manifest_json=?5");
    bind_text(row.get(), 1, item.directory);
    bind_text(row.get(), 2, rejection);
    sqlite3_bind_int64(row.get(), 3, now_us());
    bind_text(row.get(), 4, item.unavailable);
    bind_text(row.get(), 5, item.manifest);
    next(row.get());
}
} // namespace bacuri::vms::archive
