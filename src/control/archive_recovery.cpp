#include "control/media_sessions/operations.hpp"
#include "control/recovery.hpp"
#include "archive/volume.hpp"
#include "control/database.hpp"
#include "control/support.hpp"

#include <ostream>
#include <thread>

namespace bacuri::vms::control
{
namespace
{
using archive::Json;

void mark_unavailable(Database& db, const archive::Owner& owner, const Json& m, const std::string& reason)
{
    const auto updated = db.exec("UPDATE bacuri_archive.blocks SET unavailable_reason=$4 WHERE site_id=$1 "
                                 "AND recording_node_id=$2 AND block_id=$3 RETURNING 1",
                                 {owner.site, owner.node, m.at("block_id"), reason});
    if (!updated.size()) {
        // A journal entry can explain a gap but cannot prove that its video survived.
        db.exec("INSERT INTO bacuri_archive.gaps VALUES($1,$2,$3,$4,$5,'storage_failure') ON CONFLICT DO NOTHING",
                {owner.site, m.at("point_id"), m.at("binding_revision").dump(), m.at("begin_utc_us").dump(),
                 m.at("end_utc_us").dump()});
    }
}
} // namespace

void rebuild_archive(std::ostream& output)
{
    Database db;
    rebuild_archive(db, output, StorageLayout::module_schemas);
}
void rebuild_archive(Database& db, std::ostream& output, StorageLayout layout)
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
        root, owner, [&](const std::string& id) {
            ++corrupt;
            db.exec("UPDATE bacuri_archive.blocks SET unavailable_reason='corrupt' WHERE site_id=$1 "
                    "AND recording_node_id=$2 AND block_id=$3",
                    {owner.site, owner.node, id});
        }};
    const auto local_cutoff = volume.retired_through();
    if (local_cutoff != std::numeric_limits<std::int64_t>::min())
        db.exec("INSERT INTO bacuri_archive.retired_through VALUES($1,$2) ON CONFLICT(site_id) DO UPDATE SET "
                "end_us=greatest(retired_through.end_us,excluded.end_us)",
                {owner.site, std::to_string(local_cutoff)});
    db.exec("SELECT bacuri_archive.advance_cutoff($1)", {owner.site});
    auto cutoff = std::stoll(db.exec("SELECT coalesce((SELECT end_us FROM bacuri_archive.retired_through WHERE "
                                     "site_id=$1),'-9223372036854775808'::bigint)",
                                     {owner.site})
                                 .get(0));
    db.exec("DELETE FROM bacuri_archive.blocks WHERE site_id=$1 AND end_utc_us<=$2",
            {owner.site, std::to_string(cutoff)});
    db.exec("DELETE FROM bacuri_archive.gaps WHERE site_id=$1 AND end_utc_us<=$2",
            {owner.site, std::to_string(cutoff)});
    volume.visit_inventory([&](const Json& m, const std::string& reason) {
        archive::validate_manifest(m);
        if (m.at("site_id") != owner.site || m.at("recording_node_id") != owner.node) {
            throw std::runtime_error("Foreign archive journal entry");
        }
        if (m.at("end_utc_us").get<std::int64_t>() <= cutoff)
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
                           {m.dump(), std::to_string(volume.generation())});
        if (!row.size()) {
            throw std::runtime_error("Catalog identity has different content");
        }
        ++verified;
    });
    volume.visit_gaps([&](const archive::Gap& gap) {
        if (gap.end <= cutoff)
            return;
        db.exec("INSERT INTO bacuri_archive.gaps VALUES($1,$2,$3,$4,$5,$6) ON CONFLICT DO NOTHING",
                {owner.site, gap.point, std::to_string(gap.binding), std::to_string(gap.begin),
                 std::to_string(gap.end), gap.reason});
    });
    auto absent = db.exec("SELECT count(*) FROM bacuri_archive.blocks WHERE site_id=$1 AND recording_node_id=$2 "
                          "AND unavailable_reason='missing'",
                          {owner.site, owner.node});
    // A failed PostgreSQL commit keeps the fence. Its retry scans the entire inventory again.
    volume.confirm_inventory();
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
