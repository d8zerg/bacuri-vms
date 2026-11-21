#include "archive/volume.hpp"
#include "archive/mp4.hpp"
#include "archive_sql.hpp"
#include "control/support.hpp"

#include <fcntl.h>
#include <sodium.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <limits>
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
    File(const fs::path& path, int flags) : File(open(path.c_str(), flags | O_CLOEXEC | O_NOFOLLOW, 0600)) {}
    explicit File(int descriptor) : fd{descriptor}
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

void unlink_if_present(int directory, const char* name, int flags = 0)
{
    if (unlinkat(directory, name, flags) < 0 && errno != ENOENT)
        system_check(-1);
}

void finish_removal(int blocks, const std::string& name)
{
    struct stat info{};
    if (fstatat(blocks, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) < 0) {
        if (errno == ENOENT)
            return;
        system_check(-1);
    }
    if (!S_ISDIR(info.st_mode)) {
        unlink_if_present(blocks, name.c_str());
        return;
    }
    File dir{openat(blocks, name.c_str(), O_CLOEXEC | O_NOFOLLOW | O_DIRECTORY | O_RDONLY)};
    unlink_if_present(dir.fd, "media.mp4");
    unlink_if_present(dir.fd, "manifest.json");
    unlink_if_present(blocks, name.c_str(), AT_REMOVEDIR);
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

namespace
{
std::uintmax_t reserved(const fs::space_info& space, int percent)
{
    return space.capacity / 100 * percent + (space.capacity % 100) * percent / 100;
}
const char* capacity_state(int percent, const fs::space_info& space, std::uintmax_t blocked)
{
    const auto floor = reserved(space, percent);
    if (space.available <= floor || space.available - floor < blocked)
        return "recording_stopped";
    if (space.available <= reserved(space, percent + 5))
        return "low_space";
    return "normal";
}
} // namespace

Json archive_status(const fs::path& root)
{
    storage::Sqlite db{root / "journal.db", storage::OpenMode::read_only};
    db.exec("BEGIN");
    auto row = db.query("SELECT json_object('committed',count(*),'pending',count(*) FILTER "
                        "(WHERE registered_at_utc_us IS NULL AND catalog_error IS NULL),"
                        "'missing',count(*) FILTER (WHERE unavailable_reason='missing'),"
                        "'corrupt',count(*) FILTER (WHERE unavailable_reason='corrupt'),"
                        "'rejected',count(catalog_error),'last_commit_utc_us',max(committed_at_utc_us),"
                        "'media_bytes',coalesce(sum(manifest_json->>'$.media.size_bytes') FILTER (WHERE "
                        "unavailable_reason IS NULL),0)) FROM committed_blocks");
    next(row.get());
    auto result = Json::parse(column(row.get(), 0));
    auto version = db.query("PRAGMA user_version");
    next(version.get());
    auto policy =
        db.query(sqlite3_column_int(version.get(), 0) < 5
                     ? "SELECT -9223372036854775808,0,20,0,0"
                     : "SELECT cutoff_us,days,reserve_percent,removed_blocks,blocked_bytes FROM retention");
    next(policy.get());
    const auto days = sqlite3_column_int(policy.get(), 1), percent = sqlite3_column_int(policy.get(), 2);
    const auto space = fs::space(root);
    const auto floor = reserved(space, percent);
    const auto free = space.available > floor ? space.available - floor : 0;
    const auto blocked = static_cast<std::uintmax_t>(sqlite3_column_int64(policy.get(), 4));
    auto rate = db.query("SELECT sum(manifest_json->>'$.media.size_bytes')*1000000.0/"
                         "max(1000000,?1-min(manifest_json->>'$.time.received_at_utc_us')) FROM committed_blocks "
                         "WHERE (manifest_json->>'$.time.received_at_utc_us') BETWEEN ?1-300000000 AND ?1 AND "
                         "unavailable_reason IS NULL");
    sqlite3_bind_int64(rate.get(), 1, now_us());
    next(rate.get());
    const auto bytes_per_second = sqlite3_column_double(rate.get(), 0);
    const auto usable = std::min(space.capacity - floor, free + result.at("media_bytes").get<std::uintmax_t>());
    result["retention_days"] = days ? Json(days) : Json(nullptr);
    result["cutoff_utc_us"] = sqlite3_column_int64(policy.get(), 0) == std::numeric_limits<std::int64_t>::min()
                                  ? Json(nullptr)
                                  : Json(sqlite3_column_int64(policy.get(), 0));
    result["removed_blocks"] = sqlite3_column_int64(policy.get(), 3);
    result["reserve_percent"] = percent;
    result["capacity_bytes"] = space.capacity;
    result["available_bytes"] = space.available;
    result["reserve_bytes"] = floor;
    result["state"] = capacity_state(percent, space, blocked);
    result["write_bytes_per_second"] = bytes_per_second > 0 ? Json(bytes_per_second) : Json(nullptr);
    const auto depth = bytes_per_second > 0 ? static_cast<double>(usable) / bytes_per_second : 0;
    result["capacity_depth_seconds"] = bytes_per_second > 0 ? Json(depth) : Json(nullptr);
    result["estimated_depth_seconds"] =
        bytes_per_second > 0 ? Json(days ? std::min(depth, days * 86400.0) : depth) : Json(nullptr);
    result["remaining_seconds"] =
        bytes_per_second > 0 ? Json(static_cast<double>(free) / bytes_per_second) : Json(nullptr);
    return result;
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
    if (version < 0 || version > 5) {
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
    if (version < 5)
        db_.exec(sql::retention);
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
    {
        auto policy = db_.query("SELECT reserve_percent FROM retention");
        next(policy.get());
        reserve_percent_ = sqlite3_column_int(policy.get(), 0);
    }
    reap({});
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

void Volume::publish(const Block& block, const MediaWriter& write_media, const Observer& observer,
                     std::uintmax_t expected_bytes)
{
    const auto id = uuid();
    const auto blocks = root_ / "blocks";
    const auto temp = blocks / (".partial-" + id);
    {
        std::scoped_lock guard{mutex_};
        require_space(expected_bytes, 0);
    }
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
        require_space(0, manifest.at("media").at("size_bytes"));
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

std::int64_t Volume::retired_through()
{
    std::scoped_lock guard{mutex_};
    auto policy = db_.query("SELECT cutoff_us FROM retention");
    next(policy.get());
    return sqlite3_column_int64(policy.get(), 0);
}
void Volume::report_capacity(const fs::space_info& space, std::uintmax_t blocked)
{
    const auto state = capacity_state(reserve_percent_, space, blocked);
    if (state != last_capacity_state_) {
        std::fprintf(stderr, "archive capacity state=%s available_bytes=%ju reserve_percent=%d\n", state,
                     space.available, reserve_percent_);
        last_capacity_state_ = state;
    }
}

void Volume::require_space(std::uintmax_t expected_bytes, std::uintmax_t staged_bytes)
{
    const auto space = fs::space(root_);
    report_capacity(space, expected_bytes);
    const auto floor = reserved(space, reserve_percent_);
    if (space.available <= floor || space.available - floor < expected_bytes) {
        auto row = db_.query("UPDATE retention SET blocked_bytes=max(blocked_bytes,?1) WHERE blocked_bytes<?1");
        sqlite3_bind_int64(row.get(), 1, static_cast<std::int64_t>(std::max(expected_bytes, staged_bytes + 1)));
        next(row.get());
        throw std::system_error(ENOSPC, std::generic_category(), "Archive reserve reached");
    }
}

void Volume::apply_retention(const RetentionPolicy& policy, const RetirementObserver& observer)
{
    const auto [cutoff, days, percent] = policy;
    if (days < 0 || days > 36500 || percent < 1 || percent > 90)
        throw std::invalid_argument("Invalid archive retention policy");
    std::scoped_lock guard{mutex_};
    auto row = db_.query("UPDATE retention SET cutoff_us=max(cutoff_us,?1),days=?2,reserve_percent=?3 "
                         "WHERE cutoff_us<?1 OR days<>?2 OR reserve_percent<>?3");
    sqlite3_bind_int64(row.get(), 1, cutoff);
    sqlite3_bind_int(row.get(), 2, days);
    sqlite3_bind_int(row.get(), 3, percent);
    next(row.get());
    reserve_percent_ = percent;
    if (observer)
        observer(RetirementStage::authorized);
    reap(observer);
    auto pressure = db_.query("SELECT blocked_bytes FROM retention");
    next(pressure.get());
    report_capacity(fs::space(root_), sqlite3_column_int64(pressure.get(), 0));
}

void Volume::reclaim_authorized()
{
    std::scoped_lock guard{mutex_};
    reap({});
}
void Volume::reap(const RetirementObserver& observer)
{
    auto rows = db_.query("SELECT block_id,relative_directory FROM committed_blocks WHERE "
                          "manifest_json->>'end_utc_us'<=(SELECT cutoff_us FROM retention) ORDER BY "
                          "manifest_json->>'end_utc_us' LIMIT 512");
    std::vector<std::string> ids;
    while (next(rows.get())) {
        const auto id = column(rows.get(), 0);
        if (!control::canonical_id(id) || column(rows.get(), 1) != "blocks/" + id)
            throw std::runtime_error("Invalid archive retirement path");
        ids.push_back(id);
    }
    rows.reset();
    if (!ids.empty()) {
        File blocks{openat(lock_.fd, "blocks", O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW | O_RDONLY)};
        for (const auto& id : ids) {
            const auto retiring = ".expired-" + id;
            if (renameat2(blocks.fd, id.c_str(), blocks.fd, retiring.c_str(), RENAME_NOREPLACE) < 0 &&
                errno != ENOENT)
                system_check(-1);
        }
        system_check(fsync(blocks.fd));
        if (observer)
            observer(RetirementStage::renamed);
        for (const auto& id : ids)
            finish_removal(blocks.fd, ".expired-" + id);
        system_check(fsync(blocks.fd));
        if (observer)
            observer(RetirementStage::removed);
        db_.exec("SAVEPOINT retired_batch");
        try {
            auto erase = db_.query("DELETE FROM committed_blocks WHERE relative_directory=?");
            for (const auto& id : ids) {
                bind_text(erase.get(), 1, "blocks/" + id);
                next(erase.get());
                sqlite3_reset(erase.get());
            }
            db_.exec("RELEASE retired_batch");
        } catch (...) {
            db_.exec("ROLLBACK TO retired_batch; RELEASE retired_batch");
            throw;
        }
    }
    db_.exec("DELETE FROM archive_gaps WHERE id IN (SELECT id FROM archive_gaps WHERE end_us<=(SELECT cutoff_us "
             "FROM retention) ORDER BY end_us LIMIT 512)");
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
    File directory{openat(lock_.fd, "blocks", O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW | O_RDONLY)};
    system_check(fsync(directory.fd));
    for (const auto& entry : fs::directory_iterator(root_ / "blocks")) {
        const auto name = entry.path().filename().string();
        // The durable rename also records the decision if the local journal is rebuilt.
        if (name.starts_with(".expired-") && control::canonical_id(std::string_view{name}.substr(9))) {
            finish_removal(directory.fd, name);
            system_check(fsync(directory.fd));
        } else {
            recover_directory(entry, invalid);
        }
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

std::vector<std::pair<std::int64_t, std::int64_t>> Volume::pending_intervals(const std::string& point,
                                                                             std::int64_t begin, std::int64_t end,
                                                                             std::optional<std::int64_t> binding,
                                                                             std::int64_t cutoff) const
{
    storage::Sqlite journal{root_ / "journal.db", storage::OpenMode::read_only};
    auto rows = journal.query(R"sql(WITH spans AS (
            SELECT manifest_json->>'begin_utc_us' AS a,manifest_json->>'end_utc_us' AS b
            FROM committed_blocks WHERE registered_at_utc_us IS NULL AND catalog_error IS NULL AND unavailable_reason IS NULL AND b>max(?5,(SELECT cutoff_us FROM retention))
            AND manifest_json->>'point_id'=?1 AND a<?2 AND b>?3
            AND (?4 IS NULL OR manifest_json->>'binding_revision'=?4)
        ), ordered AS (
            SELECT a,b,max(b) OVER(ORDER BY a,b ROWS BETWEEN UNBOUNDED PRECEDING AND 1 PRECEDING) AS previous FROM spans
        ), grouped AS (
            SELECT a,b,sum(previous IS NULL OR a>previous) OVER(ORDER BY a,b) AS run FROM ordered
        ) SELECT min(a),max(b) FROM grouped GROUP BY run ORDER BY min(a) LIMIT 1001)sql");
    bind_text(rows.get(), 1, point);
    sqlite3_bind_int64(rows.get(), 2, end);
    sqlite3_bind_int64(rows.get(), 3, begin);
    sqlite3_bind_int64(rows.get(), 5, cutoff);
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
    auto rows = db_.query("SELECT manifest_json,coalesce(unavailable_reason,'') FROM committed_blocks WHERE "
                          "manifest_json->>'end_utc_us'>(SELECT cutoff_us FROM retention)");
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
