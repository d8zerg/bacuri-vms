#include "archive/retention.hpp"
#include "archive/volume_io.hpp"
#include "platform/identifiers.hpp"
#include <algorithm>
#include <cstdio>

namespace bacuri::vms::archive
{
using storage::bind_text;
using storage::column;
using storage::next;
namespace
{
std::uintmax_t reserved(const fs::space_info& space, int percent)
{
    const auto share = static_cast<std::uintmax_t>(std::clamp(percent, 0, 100));
    return space.capacity / 100 * share + (space.capacity % 100) * share / 100;
}
// Free space below the reserve plus this margin is reported before recording stops.
constexpr int low_space_margin_percent = 5;

} // namespace
CapacityState capacity_state(int percent, const fs::space_info& space, std::uintmax_t blocked)
{
    const auto floor = reserved(space, percent);
    if (space.available <= floor || space.available - floor < blocked)
        return CapacityState::recording_stopped;
    if (space.available <= reserved(space, percent + low_space_margin_percent))
        return CapacityState::low_space;
    return CapacityState::normal;
}
namespace
{
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

VolumeRetention::VolumeRetention(BlockJournal& journal, std::mutex& replica_mutex, int directory, bool replicated)
    : journal_{journal}, replica_mutex_{replica_mutex}, directory_{directory}, replicated_{replicated}
{
    auto policy = journal_.db_.query("SELECT reserve_percent FROM retention");
    next(policy.get());
    reserve_percent_ = sqlite3_column_int(policy.get(), 0);
}
std::int64_t VolumeRetention::retired_through()
{
    std::scoped_lock guard{journal_.mutex_};
    auto policy = journal_.db_.query("SELECT cutoff_us FROM retention");
    next(policy.get());
    return sqlite3_column_int64(policy.get(), 0);
}
void VolumeRetention::report_capacity(const fs::space_info& space, std::uintmax_t blocked)
{
    const auto state = capacity_state(reserve_percent_, space, blocked);
    if (state != last_capacity_state_) {
        std::fprintf(stderr, "archive capacity state=%s available_bytes=%ju reserve_percent=%d\n", name(state),
                     space.available, reserve_percent_);
        last_capacity_state_ = state;
    }
}

CapacityState VolumeRetention::capacity() const
{
    int percent{};
    {
        std::scoped_lock guard{journal_.mutex_};
        percent = reserve_percent_;
    }
    return capacity_state(percent, fs::space(journal_.root_), 0);
}

void VolumeRetention::require_space(std::uintmax_t expected_bytes, std::uintmax_t staged_bytes)
{
    const auto space = fs::space(journal_.root_);
    report_capacity(space, expected_bytes);
    const auto floor = reserved(space, reserve_percent_);
    if (space.available <= floor || space.available - floor < expected_bytes) {
        auto row =
            journal_.db_.query("UPDATE retention SET blocked_bytes=max(blocked_bytes,?1) WHERE blocked_bytes<?1");
        sqlite3_bind_int64(row.get(), 1, static_cast<std::int64_t>(std::max(expected_bytes, staged_bytes + 1)));
        next(row.get());
        throw std::system_error(ENOSPC, std::generic_category(), "Archive reserve reached");
    }
}

void VolumeRetention::apply_retention(const RetentionPolicy& policy, const RetirementObserver& observer)
{
    const auto [cutoff, days, percent] = policy;
    if (days < 0 || days > 36500 || percent < 1 || percent > 90)
        throw std::invalid_argument("Invalid archive retention policy");
    std::scoped_lock copy_guard{replica_mutex_};
    std::scoped_lock guard{journal_.mutex_};
    auto row = journal_.db_.query("UPDATE retention SET cutoff_us=max(cutoff_us,?1),days=?2,reserve_percent=?3 "
                                  "WHERE cutoff_us<?1 OR days<>?2 OR reserve_percent<>?3");
    sqlite3_bind_int64(row.get(), 1, cutoff);
    sqlite3_bind_int(row.get(), 2, days);
    sqlite3_bind_int(row.get(), 3, percent);
    next(row.get());
    reserve_percent_ = percent;
    if (observer)
        observer(RetirementStage::authorized);
    reap(observer);
    auto pressure = journal_.db_.query("SELECT blocked_bytes FROM retention");
    next(pressure.get());
    report_capacity(fs::space(journal_.root_), static_cast<std::uintmax_t>(std::max<sqlite3_int64>(
                                                   0, sqlite3_column_int64(pressure.get(), 0))));
}

std::map<std::string, std::int64_t> VolumeRetention::point_cutoffs()
{
    std::scoped_lock lock{journal_.mutex_};
    auto q = journal_.db_.query("SELECT point_id,cutoff_us FROM local_point_retention");
    std::map<std::string, std::int64_t> rows;
    while (next(q.get()))
        rows[column(q.get(), 0)] = sqlite3_column_int64(q.get(), 1);
    return rows;
}
void VolumeRetention::protect_blocks(const std::vector<std::string>& ids)
{
    std::scoped_lock guard{journal_.mutex_};
    journal_.db_.exec("SAVEPOINT protection_snapshot");
    try {
        auto snapshot = Json(ids).dump();
        auto add = journal_.db_.query("INSERT OR IGNORE INTO protected_blocks SELECT value FROM json_each(?)");
        bind_text(add.get(), 1, snapshot);
        next(add.get());
        auto remove = journal_.db_.query(
            "DELETE FROM protected_blocks WHERE block_id NOT IN (SELECT value FROM json_each(?))");
        bind_text(remove.get(), 1, snapshot);
        next(remove.get());
        journal_.db_.exec("RELEASE protection_snapshot");
    } catch (...) {
        journal_.db_.exec("ROLLBACK TO protection_snapshot; RELEASE protection_snapshot");
        throw;
    }
}
void VolumeRetention::remember_cutoffs(const std::map<std::string, std::int64_t>& cutoffs)
{
    std::scoped_lock lock{journal_.mutex_};
    auto q =
        journal_.db_.query("INSERT INTO local_point_retention VALUES(?1,?2) ON CONFLICT(point_id) DO UPDATE SET "
                           "cutoff_us=excluded.cutoff_us WHERE cutoff_us<excluded.cutoff_us");
    for (const auto& [point, cutoff] : cutoffs) {
        bind_text(q.get(), 1, point);
        sqlite3_bind_int64(q.get(), 2, cutoff);
        next(q.get());
        sqlite3_reset(q.get());
    }
}
void VolumeRetention::reclaim_authorized()
{
    std::scoped_lock copy_guard{replica_mutex_};
    std::scoped_lock guard{journal_.mutex_};
    reap({});
}
void VolumeRetention::reap(const RetirementObserver& observer)
{
    auto rows = journal_.db_.query(R"sql(SELECT block_id,relative_directory FROM committed_blocks WHERE
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
        File blocks{openat(directory_, "blocks", O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW | O_RDONLY)};
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
        journal_.db_.exec("SAVEPOINT retired_batch");
        try {
            auto queue =
                journal_.db_.query("INSERT OR IGNORE INTO replica_deletions SELECT ?1 WHERE ?2 OR EXISTS (SELECT "
                                   "1 FROM replica_checks WHERE block_id=?1)");
            auto clear = journal_.db_.query("DELETE FROM replica_checks WHERE block_id=?");
            auto erase = journal_.db_.query("DELETE FROM committed_blocks WHERE relative_directory=?");
            for (const auto& id : ids) {
                bind_text(queue.get(), 1, id);
                sqlite3_bind_int(queue.get(), 2, replicated_);
                next(queue.get());
                sqlite3_reset(queue.get());
                bind_text(clear.get(), 1, id);
                next(clear.get());
                sqlite3_reset(clear.get());
                bind_text(erase.get(), 1, "blocks/" + id);
                next(erase.get());
                sqlite3_reset(erase.get());
            }
            journal_.db_.exec("RELEASE retired_batch");
        } catch (...) {
            journal_.db_.exec("ROLLBACK TO retired_batch; RELEASE retired_batch");
            throw;
        }
    }
    journal_.db_.exec(
        R"sql(DELETE FROM archive_gaps WHERE id IN (SELECT id FROM archive_gaps WHERE end_us<=(SELECT cutoff_us FROM retention)
        UNION SELECT g.id FROM local_point_retention r CROSS JOIN archive_gaps g WHERE r.cutoff_us>(SELECT cutoff_us FROM retention)
        AND g.point=r.point_id AND g.end_us<=r.cutoff_us LIMIT 512))sql");
}

} // namespace bacuri::vms::archive
