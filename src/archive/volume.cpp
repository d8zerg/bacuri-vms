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
    return digest_media(file.fd);
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

Volume::Volume(const fs::path& root, Owner owner, const InvalidBlock& invalid)
    : root_{root}, lock_{root}, db_{root / "journal.db"}, identity_{std::move(owner), {}, 0}
{
    int version{};
    {
        auto version_query = db_.query("PRAGMA user_version");
        next(version_query.get());
        version = sqlite3_column_int(version_query.get(), 0);
    }
    if (version < 0 || version > 4) {
        throw std::runtime_error("Unsupported archive journal version");
    }
    if (version == 0) {
        db_.exec(sql::initial);
    }
    if (version < 2) {
        db_.exec(sql::upgrade);
    }
    if (version < 3) {
        db_.exec(sql::coverage);
    }
    if (version < 4) {
        db_.exec(sql::gap_history);
    }
    db_.exec("BEGIN IMMEDIATE");
    auto previous = db_.query("SELECT 1 FROM volume_owner");
    const bool assigned = next(previous.get());
    previous.reset();
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
    File verification{root_ / "blocks", O_RDONLY | O_DIRECTORY};
    system_check(flock(verification.fd, LOCK_EX));
    reconcile(invalid);
    if (invalid && !assigned) {
        auto evidence = db_.query("SELECT 1 FROM committed_blocks WHERE unavailable_reason IS NULL LIMIT 1");
        if (!next(evidence.get())) {
            throw std::runtime_error("Cannot establish archive volume identity from manifests");
        }
    }
    // A rebuilt journal can contain manifests from a higher previous generation.
    auto generation = db_.query("UPDATE volume_owner SET generation=max(generation,1+coalesce("
                                "(SELECT max(owner_generation) FROM committed_blocks),0)) RETURNING generation");
    next(generation.get());
    identity_.generation = sqlite3_column_int64(generation.get(), 0);
    next(generation.get());
    db_.exec("COMMIT");
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

void Volume::recover_directory(const fs::directory_entry& entry, const InvalidBlock& invalid)
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
    } else if (invalid) {
        invalid(id);
    }
}

void Volume::reconcile(const InvalidBlock& invalid)
{
    sync_path(root_ / "blocks", O_DIRECTORY);
    for (const auto& entry : fs::directory_iterator(root_ / "blocks")) {
        recover_directory(entry, invalid);
    }
    auto directories = db_.query("SELECT relative_directory,block_id,manifest_json->>'block_id' FROM "
                                 "committed_blocks ORDER BY relative_directory");
    while (next(directories.get())) {
        auto relative = column(directories.get(), 0);
        auto id = column(directories.get(), 1);
        if (!control::canonical_id(id) || relative != "blocks/" + id || column(directories.get(), 2) != id) {
            throw std::runtime_error("Invalid archive journal identity");
        }
        if (!fs::exists(fs::symlink_status(root_ / relative))) {
            availability(relative, "missing");
            std::fprintf(stderr, "archive missing %s\n", relative.c_str());
        }
    }
}

std::vector<std::pair<std::int64_t, std::int64_t>>
Volume::pending_intervals(const std::string& point, std::int64_t begin, std::int64_t end,
                          std::optional<std::int64_t> binding) const
{
    storage::Sqlite journal{root_ / "journal.db", storage::OpenMode::read_only};
    auto rows = journal.query(R"sql(WITH spans AS (
            SELECT manifest_json->>'begin_utc_us' AS a,manifest_json->>'end_utc_us' AS b
            FROM committed_blocks WHERE registered_at_utc_us IS NULL AND catalog_error IS NULL AND unavailable_reason IS NULL
            AND manifest_json->>'point_id'=? AND a<? AND b>?
            AND (?4 IS NULL OR manifest_json->>'binding_revision'=?4)
        ), ordered AS (
            SELECT a,b,max(b) OVER(ORDER BY a,b ROWS BETWEEN UNBOUNDED PRECEDING AND 1 PRECEDING) AS previous FROM spans
        ), grouped AS (
            SELECT a,b,sum(previous IS NULL OR a>previous) OVER(ORDER BY a,b) AS run FROM ordered
        ) SELECT min(a),max(b) FROM grouped GROUP BY run ORDER BY min(a) LIMIT 1001)sql");
    bind_text(rows.get(), 1, point);
    sqlite3_bind_int64(rows.get(), 2, end);
    sqlite3_bind_int64(rows.get(), 3, begin);
    if (binding) {
        sqlite3_bind_int64(rows.get(), 4, *binding);
    }
    std::vector<std::pair<std::int64_t, std::int64_t>> result;
    while (next(rows.get())) {
        result.emplace_back(sqlite3_column_int64(rows.get(), 0), sqlite3_column_int64(rows.get(), 1));
    }
    return result;
}

void Volume::visit_inventory(const std::function<void(const Json&, const std::string&)>& visit)
{
    std::scoped_lock guard{mutex_};
    auto rows = db_.query("SELECT manifest_json,coalesce(unavailable_reason,'') FROM committed_blocks");
    while (next(rows.get())) {
        visit(Json::parse(column(rows.get(), 0)), column(rows.get(), 1));
    }
}

void Volume::confirm_inventory()
{
    std::scoped_lock guard{mutex_};
    db_.exec("BEGIN IMMEDIATE");
    try {
        auto rows = db_.query("UPDATE committed_blocks SET registered_at_utc_us=?,catalog_error=NULL "
                              "WHERE registered_at_utc_us IS NULL OR catalog_error IS NOT NULL");
        sqlite3_bind_int64(rows.get(), 1, now_us());
        next(rows.get());
        db_.exec("UPDATE archive_gaps SET registered=1 WHERE registered=0; COMMIT");
    } catch (...) {
        db_.exec("ROLLBACK");
        throw;
    }
}

void Volume::visit_gaps(const std::function<void(const Gap&)>& visit)
{
    std::scoped_lock guard{mutex_};
    auto rows = db_.query("SELECT point,binding,begin_us,end_us,reason FROM archive_gaps ORDER BY id");
    while (next(rows.get())) {
        visit({column(rows.get(), 0), sqlite3_column_int64(rows.get(), 1), sqlite3_column_int64(rows.get(), 2),
               sqlite3_column_int64(rows.get(), 3), column(rows.get(), 4)});
    }
}

void Volume::report_gap(const Gap& gap)
{
    std::scoped_lock guard{mutex_};
    auto row = db_.query("INSERT INTO archive_gaps(point,binding,begin_us,end_us,reason) VALUES(?,?,?,?,?) ON "
                         "CONFLICT DO NOTHING");
    bind_text(row.get(), 1, gap.point);
    sqlite3_bind_int64(row.get(), 2, gap.binding);
    sqlite3_bind_int64(row.get(), 3, gap.begin);
    sqlite3_bind_int64(row.get(), 4, gap.end);
    bind_text(row.get(), 5, gap.reason);
    next(row.get());
}
std::optional<std::pair<std::int64_t, Gap>> Volume::pending_gap()
{
    std::scoped_lock guard{mutex_};
    auto rows = db_.query(
        "SELECT id,point,binding,begin_us,end_us,reason FROM archive_gaps WHERE registered=0 ORDER BY id LIMIT 1");
    if (!next(rows.get())) {
        return {};
    }
    auto number = [&](int col) { return sqlite3_column_int64(rows.get(), col); };
    return std::pair{number(0),
                     Gap{column(rows.get(), 1), number(2), number(3), number(4), column(rows.get(), 5)}};
}
void Volume::acknowledge_gap(std::int64_t id)
{
    std::scoped_lock guard{mutex_};
    auto row = db_.query("UPDATE archive_gaps SET registered=1 WHERE id=?");
    sqlite3_bind_int64(row.get(), 1, id);
    next(row.get());
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
