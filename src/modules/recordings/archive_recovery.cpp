#include "archive/manifest_json.hpp"
#include "modules/media_sessions/operations.hpp"
#include "modules/recordings/archive_recovery.hpp"
#include "archive/volume.hpp"
#include "storage/postgresql/database.hpp"
#include "application/control_api/support.hpp"

#include <ostream>
#include <set>
#include <thread>

namespace bacuri::vms::control
{
namespace
{
using archive::Json;

void mark_unavailable(postgresql::Database& db, const archive::Owner& owner, const archive::BlockManifest& m,
                      const std::string& reason)
{
    const auto updated = db.exec("UPDATE bacuri_archive.blocks SET unavailable_reason=$4 WHERE site_id=$1 "
                                 "AND recording_node_id=$2 AND block_id=$3 RETURNING 1",
                                 {owner.site, owner.node, m.recording.block().id.to_string(), reason});
    if (!updated.size()) {
        // A journal entry can explain a gap but cannot prove that its video survived.
        db.exec("INSERT INTO bacuri_archive.gaps VALUES($1,$2,$3,$4,$5,'storage_failure') ON CONFLICT DO NOTHING",
                {owner.site, m.recording.binding().point.to_string(),
                 std::to_string(m.recording.binding().revision),
                 std::to_string(m.recording.interval().begin().time_since_epoch().count()),
                 std::to_string(m.recording.interval().end().time_since_epoch().count())});
    }
}
} // namespace

void rebuild_archive(std::ostream& output)
{
    postgresql::Database db;
    rebuild_archive(db, output, StorageLayout::module_schemas);
}
void rebuild_archive(postgresql::Database& db, std::ostream& output, StorageLayout layout)
{
    const bool legacy = layout == StorageLayout::legacy;
    const archive::Owner owner{setting("BACURI_SITE_ID"), setting("BACURI_NODE_ID")};
    const archive::fs::path root{setting("BACURI_ARCHIVE_DIR")};
    if (db.exec("SELECT pg_try_advisory_lock(hashtextextended($1,1))", {owner.site}).get(0) != "t") {
        throw std::runtime_error("Another site recovery is in progress");
    }
    db.exec("BEGIN");
    const std::string insert =
        legacy ? "INSERT INTO bacuri_inventory.archive_recovery" : "INSERT INTO bacuri_archive.archive_recovery";
    db.exec((insert + " VALUES($1,'verifying') ON CONFLICT(site_id) DO UPDATE SET reason=excluded.reason").c_str(),
            {owner.site});
    const bool closed = media_sessions::close_archive_sessions(db, owner.site, layout);
    db.exec("COMMIT");
    if (closed) {
        std::this_thread::sleep_for(std::chrono::seconds{5});
    }
    const bool journal = archive::fs::is_regular_file(archive::fs::symlink_status(root / "journal.db"));
    if (!archive::fs::is_directory(archive::fs::symlink_status(root / "blocks")) ||
        (!journal && archive::fs::is_empty(root / "blocks"))) {
        db.exec(legacy
                    ? "UPDATE bacuri_inventory.archive_recovery SET reason='volume_unavailable' WHERE site_id=$1"
                    : "UPDATE bacuri_archive.archive_recovery SET reason='volume_unavailable' WHERE site_id=$1",
                {owner.site});
        throw std::runtime_error("Archive volume unavailable or uninitialized");
    }
    db.exec("BEGIN");
    db.exec(
        "UPDATE bacuri_archive.blocks SET unavailable_reason='missing' WHERE site_id=$1 AND recording_node_id=$2",
        {owner.site, owner.node});
    std::size_t verified{}, corrupt{}, missing{};
    archive::Volume volume{
        root,
        owner,
        {.replica = optional_setting("BACURI_ARCHIVE_REPLICA_DIR"), .invalid = [&](const std::string& id) {
             ++corrupt;
             db.exec("UPDATE bacuri_archive.blocks SET unavailable_reason='corrupt' "
                     "WHERE site_id=$1 AND recording_node_id=$2 AND block_id=$3",
                     {owner.site, owner.node, id});
         }}};
    // The rebuilt catalog reports blocks as available only after their media checksums match.
    volume.verification.verify_all();
    const auto local_cutoff = volume.retention.retired_through();
    if (local_cutoff != std::numeric_limits<std::int64_t>::min())
        db.exec("INSERT INTO bacuri_archive.retired_through VALUES($1,$2) ON CONFLICT(site_id) DO UPDATE SET "
                "end_us=greatest(retired_through.end_us,excluded.end_us)",
                {owner.site, std::to_string(local_cutoff)});
    std::set<std::string> held;
    const bool supports_holds =
        !legacy && db.exec("SELECT to_regclass('bacuri_archive.archive_holds') IS NOT NULL").get(0) == "t";
    if (supports_holds) {
        auto ids = db.exec("SELECT DISTINCT b.block_id FROM bacuri_archive.held_blocks b JOIN "
                           "bacuri_archive.archive_holds h "
                           "ON (h.site_id,h.id)=(b.site_id,b.hold_id) WHERE h.site_id=$1 AND "
                           "h.expires_us>bacuri_archive.retention_now_us()",
                           {owner.site})
                       .column();
        held.insert(ids.begin(), ids.end());
        volume.retention.protect_blocks(ids);
    }
    const std::vector<std::string> ids(held.begin(), held.end());
    const auto kept = postgresql::array_parameter(ids);
    std::map<std::string, std::int64_t> retired_points;
    if (!legacy && db.exec("SELECT to_regclass('bacuri_archive.point_retention') IS NOT NULL").get(0) == "t") {
        for (const auto& [point, end] : volume.retention.point_cutoffs())
            db.exec("INSERT INTO bacuri_archive.point_retention(site_id,point_id,cutoff_us) VALUES($1,$2,$3) ON "
                    "CONFLICT(site_id,point_id) DO UPDATE SET "
                    "cutoff_us=greatest(point_retention.cutoff_us,excluded.cutoff_us)",
                    {owner.site, point, std::to_string(end)});
        auto per_point = db.exec("SELECT point_id,bacuri_archive.point_cutoff(site_id,point_id) FROM "
                                 "bacuri_archive.point_retention WHERE site_id=$1",
                                 {owner.site});
        for (int i = 0; i < per_point.size(); ++i)
            retired_points[per_point.get(i)] = std::stoll(per_point.get(i, 1));
        db.exec(
            "DELETE FROM bacuri_archive.blocks b USING bacuri_archive.point_retention p WHERE "
            "(b.site_id,b.point_id)=(p.site_id,p.point_id) AND p.site_id=$1 AND "
            "b.end_utc_us<=bacuri_archive.point_cutoff(p.site_id,p.point_id) AND NOT (b.block_id=ANY($2::uuid[]))",
            {owner.site, kept});
        db.exec("DELETE FROM bacuri_archive.gaps g USING bacuri_archive.point_retention p WHERE "
                "(g.site_id,g.point_id)=(p.site_id,p.point_id) AND p.site_id=$1 AND "
                "g.end_utc_us<=bacuri_archive.point_cutoff(p.site_id,p.point_id)",
                {owner.site});
    }
    db.exec("SELECT bacuri_archive.advance_cutoff($1)", {owner.site});
    auto cutoff = std::stoll(db.exec("SELECT coalesce((SELECT end_us FROM bacuri_archive.retired_through WHERE "
                                     "site_id=$1),'-9223372036854775808'::bigint)",
                                     {owner.site})
                                 .get(0));
    db.exec(
        "DELETE FROM bacuri_archive.blocks WHERE site_id=$1 AND end_utc_us<=$2 AND NOT (block_id=ANY($3::uuid[]))",
        {owner.site, std::to_string(cutoff), kept});
    db.exec("DELETE FROM bacuri_archive.gaps WHERE site_id=$1 AND end_utc_us<=$2",
            {owner.site, std::to_string(cutoff)});
    const auto expired = [&](const std::string& point, std::int64_t end) {
        auto p = retired_points.find(point);
        return end <= cutoff || (p != retired_points.end() && end <= p->second);
    };
    volume.journal.visit_inventory([&](const archive::BlockManifest& m, const std::string& reason) {
        if (m.recording.block().site.to_string() != owner.site || m.recording_node.to_string() != owner.node) {
            throw std::runtime_error("Foreign archive journal entry");
        }
        if (expired(m.recording.binding().point.to_string(),
                    m.recording.interval().end().time_since_epoch().count()) &&
            !held.contains(m.recording.block().id.to_string()))
            return;
        if (!reason.empty()) {
            mark_unavailable(db, owner, m, reason);
            if (reason == "corrupt") {
                ++corrupt;
            } else {
                ++missing;
            }
            return;
        }
        auto row = db.exec("INSERT INTO bacuri_archive.blocks(manifest,verification_generation) VALUES($1,$2) "
                           "ON CONFLICT(site_id,block_id,recording_epoch_id,owner_generation) DO UPDATE SET "
                           "unavailable_reason=NULL,verification_generation=excluded.verification_generation "
                           "WHERE blocks.manifest=excluded.manifest RETURNING 1",
                           {archive::manifest_json(m).dump(), std::to_string(volume.generation())});
        if (!row.size()) {
            throw std::runtime_error("Catalog identity has different content");
        }
        ++verified;
    });
    volume.journal.visit_gaps([&](const archive::Gap& gap) {
        if (expired(gap.point, gap.end))
            return;
        db.exec("INSERT INTO bacuri_archive.gaps VALUES($1,$2,$3,$4,$5,$6) ON CONFLICT DO NOTHING",
                {owner.site, gap.point, std::to_string(gap.binding), std::to_string(gap.begin),
                 std::to_string(gap.end), gap.reason});
    });
    auto absent = db.exec("SELECT count(*) FROM bacuri_archive.blocks WHERE site_id=$1 AND recording_node_id=$2 "
                          "AND unavailable_reason='missing'",
                          {owner.site, owner.node});
    // A failed PostgreSQL commit keeps the fence. Its retry scans the entire inventory again.
    volume.journal.confirm_inventory();
    db.exec(legacy ? "DELETE FROM bacuri_inventory.archive_recovery WHERE site_id=$1"
                   : "DELETE FROM bacuri_archive.archive_recovery WHERE site_id=$1",
            {owner.site});
    db.exec("COMMIT");
    output << Json{{"verified", verified},
                   {"corrupt", corrupt},
                   {"journal_missing", missing},
                   {"catalog_missing", std::stoll(absent.get(0))}}
                  .dump()
           << '\n';
}
} // namespace bacuri::vms::control
