#include "archive/manifest_json.hpp"
#include "archive/block_journal.hpp"
#include "archive/volume_io.hpp"
#include "platform/identifiers.hpp"
#include "observability/metrics.hpp"
#include "archive_sql.hpp"

namespace bacuri::vms::archive
{
using storage::bind_text;
using storage::column;
using storage::next;
BlockJournal::BlockJournal(const fs::path& root, Owner owner)
    : root_{root}, db_{root / "journal.db"}, identity_{std::move(owner), {}, 0}
{
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
}
bool BlockJournal::assign_owner()
{
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
    return assigned;
}
void BlockJournal::finish_open(bool require_manifests, bool assigned)
{
    if (require_manifests && !assigned) {
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
bool BlockJournal::overlaps(const Block& block)
{
    auto overlaps = db_.query("SELECT 1 FROM committed_blocks WHERE manifest_json->>'point_id'=?1 "
                              "AND manifest_json->>'binding_revision'=?2 AND manifest_json->>'begin_utc_us'<?3 "
                              "AND manifest_json->>'end_utc_us'>?4 AND unavailable_reason IS NULL LIMIT 1");
    bind_text(overlaps.get(), 1, block.origin.point);
    sqlite3_bind_int64(overlaps.get(), 2, block.origin.binding);
    sqlite3_bind_int64(overlaps.get(), 3, block.end.time_since_epoch().count());
    sqlite3_bind_int64(overlaps.get(), 4, block.first.utc().time_since_epoch().count());
    return next(overlaps.get());
}

bool BlockJournal::contains(const std::string& relative)
{
    auto row = db_.query("SELECT 1 FROM committed_blocks WHERE relative_directory=?");
    bind_text(row.get(), 1, relative);
    return next(row.get());
}

void BlockJournal::mark_missing_directories()
{
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

void BlockJournal::record(const std::string& relative, const BlockManifest& m)
{
    // The caller computed the media checksums from the directory being journaled.
    auto save = db_.query("INSERT INTO committed_blocks(site_id,block_id,recording_epoch_id,owner_generation,"
                          "manifest_json,relative_directory,committed_at_utc_us,verified_generation,"
                          "verified_at_utc_us) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?7)");
    bind_text(save.get(), 1, identity_.owner.site);
    bind_text(save.get(), 2, m.recording.block().id.to_string());
    bind_text(save.get(), 3, m.recording.block().recording_epoch.to_string());
    sqlite3_bind_int64(save.get(), 4, m.recording.block().owner_generation);
    bind_text(save.get(), 5, manifest_json(m).dump());
    bind_text(save.get(), 6, relative);
    sqlite3_bind_int64(save.get(), 7, now_us());
    sqlite3_bind_int64(save.get(), 8, identity_.generation);
    next(save.get());
}

void BlockJournal::journal_published(const std::function<void(CommitStage)>& observer)
{
    if (published_) {
        sync_path(root_ / "blocks", O_DIRECTORY);
        if (observer)
            observer(CommitStage::parent_synced);
        record(published_->first, published_->second);
        published_.reset();
        if (observer)
            observer(CommitStage::journaled);
    }
}

bool BlockJournal::availability(const std::string& relative, const std::string& reason)
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

std::vector<std::pair<std::int64_t, std::int64_t>>
BlockJournal::pending_intervals(const std::string& point, std::int64_t begin, std::int64_t end,
                                std::optional<std::int64_t> binding, std::int64_t cutoff) const
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

void BlockJournal::visit_inventory(const std::function<void(const BlockManifest&, const std::string&)>& visit)
{
    std::scoped_lock guard{mutex_};
    auto rows =
        db_.query("SELECT manifest_json,coalesce(unavailable_reason,'') FROM committed_blocks WHERE "
                  "manifest_json->>'end_utc_us'>max((SELECT cutoff_us FROM retention),coalesce((SELECT cutoff_us "
                  "FROM local_point_retention WHERE point_id=manifest_json->>'point_id'),-9223372036854775808))");
    while (next(rows.get())) {
        visit(parse_manifest(Json::parse(column(rows.get(), 0))), column(rows.get(), 1));
    }
}

void BlockJournal::confirm_inventory()
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

void BlockJournal::visit_gaps(const std::function<void(const Gap&)>& visit)
{
    std::scoped_lock guard{mutex_};
    auto rows = db_.query("SELECT point,binding,begin_us,end_us,reason FROM archive_gaps ORDER BY id");
    while (next(rows.get())) {
        visit({column(rows.get(), 0), sqlite3_column_int64(rows.get(), 1), sqlite3_column_int64(rows.get(), 2),
               sqlite3_column_int64(rows.get(), 3), column(rows.get(), 4)});
    }
}

void BlockJournal::report_gap(const Gap& gap)
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
std::optional<std::pair<std::int64_t, Gap>> BlockJournal::pending_gap()
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
void BlockJournal::acknowledge_gap(std::int64_t id)
{
    std::scoped_lock guard{mutex_};
    auto row = db_.query("UPDATE archive_gaps SET registered=1 WHERE id=?");
    sqlite3_bind_int64(row.get(), 1, id);
    next(row.get());
}

Pending::Pending(std::string path, std::string stored, std::string reason)
    : directory{std::move(path)}, manifest{parse_manifest(Json::parse(stored))}, unavailable{std::move(reason)},
      stored_{std::move(stored)}
{
}

std::optional<Pending> BlockJournal::pending()
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

void BlockJournal::acknowledge(const Pending& item, const std::string& rejection)
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
    bind_text(row.get(), 5, item.stored_);
    next(row.get());
}
} // namespace bacuri::vms::archive
