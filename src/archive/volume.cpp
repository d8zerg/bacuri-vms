#include "archive/volume.hpp"
#include "archive/mp4.hpp"
#include "archive/replica.hpp"
#include "archive_sql.hpp"
#include "platform/identifiers.hpp"
#include "observability/metrics.hpp"

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
    unlink_if_present(dir.fd, "audio.pcm");
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
    const auto share = static_cast<std::uintmax_t>(std::clamp(percent, 0, 100));
    return space.capacity / 100 * share + (space.capacity % 100) * share / 100;
}
// Free space below the reserve plus this margin is reported before recording stops.
constexpr int low_space_margin_percent = 5;

CapacityState capacity_state(int percent, const fs::space_info& space, std::uintmax_t blocked)
{
    const auto floor = reserved(space, percent);
    if (space.available <= floor || space.available - floor < blocked)
        return CapacityState::recording_stopped;
    if (space.available <= reserved(space, percent + low_space_margin_percent))
        return CapacityState::low_space;
    return CapacityState::normal;
}
const char* name(CapacityState state)
{
    switch (state) {
    case CapacityState::recording_stopped:
        return "recording_stopped";
    case CapacityState::low_space:
        return "low_space";
    case CapacityState::normal:
        break;
    }
    return "normal";
}
} // namespace

Json archive_status(const fs::path& root, const std::optional<fs::path>& replica)
{
    storage::Sqlite db{root / "journal.db", storage::OpenMode::read_only};
    db.exec("BEGIN");
    auto row = db.query("SELECT json_object('committed',count(*),'pending',count(*) FILTER "
                        "(WHERE registered_at_utc_us IS NULL AND catalog_error IS NULL),"
                        "'missing',count(*) FILTER (WHERE unavailable_reason='missing'),"
                        "'corrupt',count(*) FILTER (WHERE unavailable_reason='corrupt'),"
                        "'rejected',count(catalog_error),'last_commit_utc_us',max(committed_at_utc_us),"
                        "'media_bytes',coalesce(sum((manifest_json->>'$.media.size_bytes') + "
                        "coalesce(manifest_json->>'$.audio.size_bytes',0)) FILTER (WHERE "
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
    if (sqlite3_column_int(version.get(), 0) >= 7 && replica) {
        auto copies = db.query("SELECT count(*) FILTER(WHERE verified=1),count(*) FILTER(WHERE verified=0),"
                               "coalesce(max(checked_us),0) FROM replica_checks");
        next(copies.get());
        auto verified = sqlite3_column_int64(copies.get(), 0), failed = sqlite3_column_int64(copies.get(), 1);
        std::error_code error;
        auto status = fs::symlink_status(*replica, error);
        const bool online = !error && fs::is_directory(status);
        result["replica"] = {
            {"verified", verified},
            {"failed", failed},
            {"online", online},
            {"pending", std::max<std::int64_t>(0, result.at("committed").get<std::int64_t>() - verified - failed)},
            {"last_checked_us", sqlite3_column_int64(copies.get(), 2)}};
    }
    const auto days = sqlite3_column_int(policy.get(), 1), percent = sqlite3_column_int(policy.get(), 2);
    const auto space = fs::space(root);
    const auto floor = reserved(space, percent);
    const auto free = space.available > floor ? space.available - floor : 0;
    const auto blocked = static_cast<std::uintmax_t>(sqlite3_column_int64(policy.get(), 4));
    auto rate = db.query("SELECT sum((manifest_json->>'$.media.size_bytes') + "
                         "coalesce(manifest_json->>'$.audio.size_bytes',0))*1000000.0/"
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
    result["state"] = name(capacity_state(percent, space, blocked));
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

Volume::Volume(const fs::path& root, Owner owner, VolumeOptions options)
    : root_{root}, replica_{std::move(options.replica)}, lock_{root}, db_{root / "journal.db"},
      identity_{std::move(owner), {}, 0}
{
    const auto& invalid = options.invalid;
    int version{};
    {
        auto version_query = db_.query("PRAGMA user_version");
        next(version_query.get());
        version = sqlite3_column_int(version_query.get(), 0);
    }
    if (version < 0 || version > 9) {
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
    if (version < 6)
        db_.exec(sql::policies);
    if (version < 7)
        db_.exec(sql::holds);
    if (version < 8)
        db_.exec(sql::verification);
    if (version < 9)
        db_.exec(sql::keyframe_gaps);
    {
        auto saved = db_.query("SELECT payload FROM recording_plan");
        if (next(saved.get()))
            update_recording_plan(column(saved.get(), 0));
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
    {
        auto policy = db_.query("SELECT reserve_percent FROM retention");
        next(policy.get());
        reserve_percent_ = sqlite3_column_int(policy.get(), 0);
    }
    if (!invalid)
        reap({});
    reconcile(invalid);
    if (invalid && !assigned) {
        auto evidence = db_.query("SELECT 1 FROM committed_blocks WHERE unavailable_reason IS NULL LIMIT 1");
        if (!next(evidence.get())) {
            throw std::runtime_error("Cannot establish archive volume identity from manifests");
        }
    }
    // A rebuilt journal can contain manifests from a higher previous generation.
    const auto opened = identity_.generation;
    auto generation = db_.query("UPDATE volume_owner SET generation=max(generation,1+coalesce("
                                "(SELECT max(owner_generation) FROM committed_blocks),0)) RETURNING generation");
    next(generation.get());
    identity_.generation = sqlite3_column_int64(generation.get(), 0);
    next(generation.get());
    generation.reset();
    if (identity_.generation != opened) {
        // Directories imported above were verified while this volume was open.
        auto imported =
            db_.query("UPDATE committed_blocks SET verified_generation=?1 WHERE verified_generation=?2");
        sqlite3_bind_int64(imported.get(), 1, identity_.generation);
        sqlite3_bind_int64(imported.get(), 2, opened);
        next(imported.get());
    }
    db_.exec("COMMIT");
}

void Volume::record(const std::string& relative, const Json& m)
{
    // The caller computed the media checksums from the directory being journaled.
    auto save = db_.query("INSERT INTO committed_blocks(site_id,block_id,recording_epoch_id,owner_generation,"
                          "manifest_json,relative_directory,committed_at_utc_us,verified_generation,"
                          "verified_at_utc_us) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?7)");
    bind_text(save.get(), 1, identity_.owner.site);
    bind_text(save.get(), 2, m.at("block_id"));
    bind_text(save.get(), 3, m.at("recording_epoch_id"));
    sqlite3_bind_int64(save.get(), 4, m.at("owner_generation"));
    bind_text(save.get(), 5, m.dump());
    bind_text(save.get(), 6, relative);
    sqlite3_bind_int64(save.get(), 7, now_us());
    sqlite3_bind_int64(save.get(), 8, identity_.generation);
    next(save.get());
}

bool Volume::publish(const Block& block, const MediaWriter& write_media, const Observer& observer,
                     std::uintmax_t expected_bytes, RecordedSound sound)
{
    const auto [audio_start, pcm] = sound;
    const auto id = uuid();
    const auto blocks = root_ / "blocks";
    const auto temp = blocks / (".partial-" + id);
    {
        std::scoped_lock guard{mutex_};
        require_space(expected_bytes + pcm.size(), 0);
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
            manifest["audio"] = {
                {"start_us", audio_start}, {"size_bytes", digest.bytes}, {"sha256", digest.sha256}};
            validate_manifest(manifest);
        }
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
        if (!block.edge_recording.empty()) {
            auto overlaps =
                db_.query("SELECT 1 FROM committed_blocks WHERE manifest_json->>'point_id'=?1 "
                          "AND manifest_json->>'binding_revision'=?2 AND manifest_json->>'begin_utc_us'<?3 "
                          "AND manifest_json->>'end_utc_us'>?4 AND unavailable_reason IS NULL LIMIT 1");
            bind_text(overlaps.get(), 1, block.origin.point);
            sqlite3_bind_int64(overlaps.get(), 2, block.origin.binding);
            sqlite3_bind_int64(overlaps.get(), 3, block.end.time_since_epoch().count());
            sqlite3_bind_int64(overlaps.get(), 4, block.first.utc().time_since_epoch().count());
            if (next(overlaps.get())) {
                fs::remove_all(temp);
                return false;
            }
        }
        require_space(0, manifest.at("media").at("size_bytes").get<std::uintmax_t>() + pcm.size());
        system_check(renameat2(AT_FDCWD, temp.c_str(), AT_FDCWD, (blocks / id).c_str(), RENAME_NOREPLACE));
        published_.emplace(std::move(relative), std::move(manifest));
        notify(observer, CommitStage::renamed);
        journal_published(observer);
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
        std::fprintf(stderr, "archive capacity state=%s available_bytes=%ju reserve_percent=%d\n", name(state),
                     space.available, reserve_percent_);
        last_capacity_state_ = state;
    }
}

CapacityState Volume::capacity() const
{
    int percent{};
    {
        std::scoped_lock guard{mutex_};
        percent = reserve_percent_;
    }
    return capacity_state(percent, fs::space(root_), 0);
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
    std::scoped_lock copy_guard{replica_mutex_};
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
    report_capacity(fs::space(root_), static_cast<std::uintmax_t>(
                                          std::max<sqlite3_int64>(0, sqlite3_column_int64(pressure.get(), 0))));
}

void Volume::update_recording_plan(const std::string& payload)
{
    std::scoped_lock lock{plan_mutex_};
    if (plan_payload_ == payload)
        return;
    std::map<std::string, std::shared_ptr<const CaptureRule>> next_rules;
    auto data = Json::parse(payload);
    for (const auto& [point, entry] : data.items()) {
        auto rule = parse_capture_rule(entry.at("rule"));
        rule.id = entry.at("id");
        rule.revision = entry.at("revision");
        rule.since = entry.at("since");
        for (const auto& w : entry.at("windows"))
            rule.windows.push_back({w.at(0), w.at(1), w.at(2)});
        next_rules.emplace(point, std::make_shared<const CaptureRule>(std::move(rule)));
    }
    std::scoped_lock guard{mutex_};
    auto save = db_.query("INSERT INTO recording_plan VALUES(1,?) ON CONFLICT(singleton) DO UPDATE SET "
                          "payload=excluded.payload WHERE payload<>excluded.payload");
    bind_text(save.get(), 1, payload);
    next(save.get());
    recording_rules_ = std::move(next_rules);
    plan_payload_ = payload;
}
std::shared_ptr<const CaptureRule> Volume::recording_rule(const std::string& point)
{
    std::scoped_lock lock{plan_mutex_};
    static const auto fallback = std::make_shared<const CaptureRule>();
    if (plan_payload_.empty())
        return {};
    auto it = recording_rules_.find(point);
    return it == recording_rules_.end() ? fallback : it->second;
}
std::map<std::string, std::int64_t> Volume::point_cutoffs()
{
    std::scoped_lock lock{mutex_};
    auto q = db_.query("SELECT point_id,cutoff_us FROM local_point_retention");
    std::map<std::string, std::int64_t> rows;
    while (next(q.get()))
        rows[column(q.get(), 0)] = sqlite3_column_int64(q.get(), 1);
    return rows;
}
void Volume::protect_blocks(const std::vector<std::string>& ids)
{
    std::scoped_lock guard{mutex_};
    db_.exec("SAVEPOINT protection_snapshot");
    try {
        auto snapshot = Json(ids).dump();
        auto add = db_.query("INSERT OR IGNORE INTO protected_blocks SELECT value FROM json_each(?)");
        bind_text(add.get(), 1, snapshot);
        next(add.get());
        auto remove =
            db_.query("DELETE FROM protected_blocks WHERE block_id NOT IN (SELECT value FROM json_each(?))");
        bind_text(remove.get(), 1, snapshot);
        next(remove.get());
        db_.exec("RELEASE protection_snapshot");
    } catch (...) {
        db_.exec("ROLLBACK TO protection_snapshot; RELEASE protection_snapshot");
        throw;
    }
}
void Volume::remember_cutoffs(const std::map<std::string, std::int64_t>& cutoffs)
{
    std::scoped_lock lock{mutex_};
    auto q = db_.query("INSERT INTO local_point_retention VALUES(?1,?2) ON CONFLICT(point_id) DO UPDATE SET "
                       "cutoff_us=excluded.cutoff_us WHERE cutoff_us<excluded.cutoff_us");
    for (const auto& [point, cutoff] : cutoffs) {
        bind_text(q.get(), 1, point);
        sqlite3_bind_int64(q.get(), 2, cutoff);
        next(q.get());
        sqlite3_reset(q.get());
    }
}
bool Volume::scrub_copy()
{
    auto replica = replica_blocks(replica_, root_, owner());
    if (!replica)
        return false;
    fs::create_directory(*replica);
    if (!fs::is_directory(fs::symlink_status(*replica)))
        throw std::runtime_error("Invalid archive copy volume");
    std::scoped_lock disk{replica_mutex_};
    std::string id;
    Json manifest;
    {
        std::scoped_lock guard{mutex_};
        auto expired = db_.query("SELECT block_id FROM replica_deletions LIMIT 1");
        if (next(expired.get())) {
            id = column(expired.get(), 0);
            expired.reset();
            if (!platform::canonical_id(id))
                throw std::runtime_error("Invalid expired copy");
            remove_copy(*replica / id);
            auto done = db_.query("DELETE FROM replica_deletions WHERE block_id=?");
            bind_text(done.get(), 1, id);
            next(done.get());
            return true;
        }
        auto row = db_.query(
            "SELECT b.block_id,b.manifest_json FROM committed_blocks b LEFT JOIN replica_checks c USING(block_id) "
            "WHERE c.checked_us IS NULL OR c.checked_us<? ORDER BY c.checked_us NULLS FIRST,b.committed_at_utc_us "
            "LIMIT 1");
        sqlite3_bind_int64(row.get(), 1, now_us() - 60000000);
        if (!next(row.get()))
            return false;
        id = column(row.get(), 0);
        manifest = Json::parse(column(row.get(), 1));
    }
    validate_manifest(manifest);
    if (!platform::canonical_id(id) || manifest.at("block_id") != id)
        throw std::runtime_error("Invalid archive copy identity");
    auto primary = root_ / "blocks" / id, copy = *replica / id;
    bool local = verified_copy(primary, manifest), remote = verified_copy(copy, manifest);
    try {
        if (local != remote) {
            const auto& target = local ? copy : primary;
            const auto free = fs::space(target.parent_path());
            const auto bytes =
                manifest.at("media").at("size_bytes").get<std::uintmax_t>() +
                (manifest.contains("audio") ? manifest.at("audio").at("size_bytes").get<std::uintmax_t>() : 0) +
                manifest.dump().size();
            int percent = 0;
            {
                std::scoped_lock guard{mutex_};
                auto policy = db_.query("SELECT reserve_percent FROM retention");
                next(policy.get());
                percent = sqlite3_column_int(policy.get(), 0);
            }
            if (capacity_state(percent, free, bytes) != CapacityState::recording_stopped) {
                restore_copy(local ? primary : copy, manifest, target);
                local = remote = true;
            }
        }
    } catch (const std::exception&) {
        std::fputs("Archive copy repair deferred\n", stderr);
    }
    std::scoped_lock guard{mutex_};
    auto existing = db_.query("SELECT 1 FROM committed_blocks WHERE block_id=?");
    bind_text(existing.get(), 1, id);
    if (!next(existing.get()))
        return false;
    auto save = db_.query("INSERT OR REPLACE INTO replica_checks VALUES(?,?,?)");
    bind_text(save.get(), 1, id);
    sqlite3_bind_int64(save.get(), 2, now_us());
    sqlite3_bind_int(save.get(), 3, local && remote);
    next(save.get());
    availability("blocks/" + id, local || remote ? "" : "corrupt");
    return true;
}
void Volume::reclaim_authorized()
{
    std::scoped_lock copy_guard{replica_mutex_};
    std::scoped_lock guard{mutex_};
    reap({});
}
void Volume::reap(const RetirementObserver& observer)
{
    auto rows = db_.query(R"sql(SELECT block_id,relative_directory FROM committed_blocks WHERE
      manifest_json->>'end_utc_us'<=(SELECT cutoff_us FROM retention) AND block_id NOT IN (SELECT block_id FROM protected_blocks)
      UNION SELECT b.block_id,b.relative_directory FROM local_point_retention r CROSS JOIN committed_blocks b
      WHERE r.cutoff_us>(SELECT cutoff_us FROM retention) AND b.manifest_json->>'point_id'=r.point_id
        AND b.manifest_json->>'end_utc_us'<=r.cutoff_us AND b.block_id NOT IN (SELECT block_id FROM protected_blocks) LIMIT 512)sql");
    std::vector<std::string> ids;
    while (next(rows.get())) {
        const auto id = column(rows.get(), 0);
        if (!platform::canonical_id(id) || column(rows.get(), 1) != "blocks/" + id)
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
            auto queue = db_.query("INSERT OR IGNORE INTO replica_deletions SELECT ?1 WHERE ?2 OR EXISTS (SELECT "
                                   "1 FROM replica_checks WHERE block_id=?1)");
            auto clear = db_.query("DELETE FROM replica_checks WHERE block_id=?");
            auto erase = db_.query("DELETE FROM committed_blocks WHERE relative_directory=?");
            for (const auto& id : ids) {
                bind_text(queue.get(), 1, id);
                sqlite3_bind_int(queue.get(), 2, replica_.has_value());
                next(queue.get());
                sqlite3_reset(queue.get());
                bind_text(clear.get(), 1, id);
                next(clear.get());
                sqlite3_reset(clear.get());
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
    db_.exec(
        R"sql(DELETE FROM archive_gaps WHERE id IN (SELECT id FROM archive_gaps WHERE end_us<=(SELECT cutoff_us FROM retention)
        UNION SELECT g.id FROM local_point_retention r CROSS JOIN archive_gaps g WHERE r.cutoff_us>(SELECT cutoff_us FROM retention)
        AND g.point=r.point_id AND g.end_us<=r.cutoff_us LIMIT 512))sql");
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

bool Volume::availability(const std::string& relative, const std::string& reason)
{
    // A changed verdict is registered again with the current verification generation.
    auto update =
        db_.query("UPDATE committed_blocks SET unavailable_reason=nullif(?2,''),registered_at_utc_us=NULL,"
                  "catalog_error=NULL WHERE relative_directory=?1 AND unavailable_reason IS NOT nullif(?2,'')");
    bind_text(update.get(), 1, relative);
    bind_text(update.get(), 2, reason);
    next(update.get());
    return db_.changes() > 0;
}

std::optional<VerificationResult> Volume::verify_next()
{
    std::scoped_lock disk{replica_mutex_};
    std::string relative, journaled;
    {
        std::scoped_lock guard{mutex_};
        // Blocks that may have been repaired come first, then blocks not verified since the volume
        // opened, then blocks whose last verification is older than the periodic interval.
        static constexpr const char* due[] = {
            "SELECT relative_directory,manifest_json FROM committed_blocks "
            "WHERE unavailable_reason IS NOT NULL AND verified_generation<?1 LIMIT 1",
            "SELECT relative_directory,manifest_json FROM committed_blocks "
            "WHERE verified_generation<?1 ORDER BY verified_generation LIMIT 1",
            "SELECT relative_directory,manifest_json FROM committed_blocks "
            "WHERE verified_at_utc_us<?2 ORDER BY verified_at_utc_us LIMIT 1"};
        const auto stale =
            now_us() - std::chrono::duration_cast<domain::Microseconds>(reverification_interval).count();
        for (const auto* query : due) {
            auto row = db_.query(query);
            sqlite3_bind_int64(row.get(), 1, identity_.generation);
            sqlite3_bind_int64(row.get(), 2, stale);
            if (next(row.get())) {
                relative = column(row.get(), 0);
                journaled = column(row.get(), 1);
                break;
            }
        }
    }
    if (relative.empty())
        return {};
    const auto manifest = Json::parse(journaled);
    validate_manifest(manifest);
    const auto id = manifest.at("block_id").get<std::string>();
    if (!platform::canonical_id(id) || relative != "blocks/" + id)
        throw std::runtime_error("Invalid archive journal identity");
    const auto media_bytes =
        manifest.at("media").at("size_bytes").get<std::uintmax_t>() +
        (manifest.contains("audio") ? manifest.at("audio").at("size_bytes").get<std::uintmax_t>() : 0);
    const auto primary = root_ / relative;
    VerificationResult result{
        .block_id = id, .bytes_read = media_bytes, .available = verified_copy(primary, manifest)};
    if (!result.available) {
        try {
            if (const auto copies = replica_blocks(replica_, root_, owner());
                copies && verified_copy(*copies / id, manifest)) {
                restore_copy(*copies / id, manifest, primary);
                result.available = true;
                result.bytes_read += 2 * media_bytes;
            }
        } catch (const std::exception& error) {
            std::fprintf(stderr, "archive copy repair deferred %s: %s\n", id.c_str(), error.what());
        }
    }
    const auto reason = result.available ? "" : fs::exists(fs::symlink_status(primary)) ? "corrupt" : "missing";
    std::scoped_lock guard{mutex_};
    auto verified = db_.query("UPDATE committed_blocks SET verified_generation=?1,verified_at_utc_us=?2 "
                              "WHERE relative_directory=?3 AND manifest_json=?4");
    sqlite3_bind_int64(verified.get(), 1, identity_.generation);
    sqlite3_bind_int64(verified.get(), 2, now_us());
    bind_text(verified.get(), 3, relative);
    bind_text(verified.get(), 4, journaled);
    next(verified.get());
    // The row is gone when retention removed the block during verification.
    if (db_.changes() > 0 && availability(relative, reason))
        std::fprintf(stderr, "archive block %s %s\n", id.c_str(), *reason ? reason : "available");
    return result;
}

void Volume::verify_all()
{
    while (verify_next()) {
    }
}

void Volume::recover_directory(const fs::directory_entry& entry, const InvalidBlock& invalid)
{
    const auto id = entry.path().filename().string();
    if (!platform::canonical_id(id)) {
        return;
    }
    const auto relative = "blocks/" + id;
    {
        auto journaled = db_.query("SELECT 1 FROM committed_blocks WHERE relative_directory=?");
        bind_text(journaled.get(), 1, relative);
        if (next(journaled.get())) {
            // Background verification checks the media of journaled blocks.
            return;
        }
    }
    // A directory published just before a crash, or every directory after the journal was rebuilt.
    std::optional<Json> verified;
    try {
        if (!fs::is_directory(entry.symlink_status())) {
            throw std::invalid_argument("Invalid block directory");
        }
        auto m = read_manifest(entry.path() / "manifest.json");
        validate_manifest(m);
        if (m.at("block_id") != id || m.at("site_id") != identity_.owner.site ||
            m.at("recording_node_id") != identity_.owner.node) {
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
        if (m.contains("audio")) {
            const auto path = entry.path() / "audio.pcm";
            if (!fs::is_regular_file(fs::symlink_status(path)) ||
                fs::file_size(path) != m["audio"]["size_bytes"].get<std::uintmax_t>())
                throw std::invalid_argument("Invalid archive audio");
            File audio{path, O_RDONLY | O_NONBLOCK};
            const auto audio_digest = digest_media(audio.fd);
            if (m["audio"]["size_bytes"] != audio_digest.bytes || m["audio"]["sha256"] != audio_digest.sha256)
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
        record(relative, *verified);
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
    auto directories = db_.query("SELECT relative_directory,block_id,manifest_json->>'block_id' FROM "
                                 "committed_blocks ORDER BY relative_directory");
    while (next(directories.get())) {
        auto relative = column(directories.get(), 0);
        auto id = column(directories.get(), 1);
        if (!platform::canonical_id(id) || relative != "blocks/" + id || column(directories.get(), 2) != id) {
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
            FROM committed_blocks WHERE registered_at_utc_us IS NULL AND catalog_error IS NULL AND unavailable_reason IS NULL AND b>max(?5,(SELECT cutoff_us FROM retention),coalesce((SELECT cutoff_us FROM local_point_retention WHERE point_id=?1),-9223372036854775808))
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
    auto rows =
        db_.query("SELECT manifest_json,coalesce(unavailable_reason,'') FROM committed_blocks WHERE "
                  "manifest_json->>'end_utc_us'>max((SELECT cutoff_us FROM retention),coalesce((SELECT cutoff_us "
                  "FROM local_point_retention WHERE point_id=manifest_json->>'point_id'),-9223372036854775808))");
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
    if (db_.changes() > 0)
        observability::Registry::process()
            .counter({.name = "bacuri_archive_gaps_total", .help = "Recording gaps journaled by reason."},
                     {{"reason", gap.reason}})
            .increment();
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
