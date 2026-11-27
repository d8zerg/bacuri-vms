#include "storage/postgresql/migrations.hpp"
#include "storage/postgresql/database.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>

namespace bacuri::vms::postgresql
{
void migrate()
{
    Database db;
    db.exec("SELECT pg_advisory_lock(486324017)");
    int version = 0;
    if (db.exec("SELECT to_regclass('bacuri_meta.schema_migrations') IS NOT NULL").get(0) == "t") {
        version = std::stoi(db.exec(schema_version_query).get(0));
    } else if (db.exec("SELECT to_regclass('bacuri_inventory.schema_migrations') IS NOT NULL").get(0) == "t") {
        version = std::stoi(db.exec(legacy_schema_version_query).get(0));
    }
    constexpr std::array migrations{"0001_initial.sql",
                                    "0002_control.sql",
                                    "0003_archive.sql",
                                    "0004_playback.sql",
                                    "0005_live.sql",
                                    "0006_alerts.sql",
                                    "0007_recovery.sql",
                                    "0008_camera_bindings.sql",
                                    "0009_ptz_commands.sql",
                                    "0010_maps.sql",
                                    "0011_alert_work.sql",
                                    "0012_archive_retention.sql",
                                    "0013_fragment_export.sql",
                                    "0014_admin_mfa.sql",
                                    "0015_credential_recovery.sql",
                                    "0016_module_schemas.sql",
                                    "0017_map_packages.sql",
                                    "0018_camera_orientation.sql",
                                    "0019_plan_georeference.sql",
                                    "0020_query_indexes.sql",
                                    "0021_access_policy.sql",
                                    "0022_camera_batches.sql",
                                    "0023_ptz_zoom.sql",
                                    "0024_camera_controls.sql",
                                    "0025_monitoring.sql",
                                    "0026_recording_policies.sql",
                                    "0027_archive_holds.sql",
                                    "0028_motion_index_queue.sql",
                                    "0029_keyframe_gaps.sql",
                                    "0030_audit_details.sql",
                                    "0031_investigation_owner.sql",
                                    "0032_desktop_sessions.sql",
                                    "0033_node_acceleration.sql"};
    if (version > static_cast<int>(migrations.size())) {
        throw std::runtime_error("Unsupported database schema");
    }

    for (const auto name : std::span{migrations}.subspan(static_cast<std::size_t>(version))) {
        std::ifstream file(std::filesystem::path{"storage/postgresql/migrations"} / name);
        if (!file) {
            throw std::runtime_error("Migration file unavailable");
        }
        std::string sql{std::istreambuf_iterator<char>{file}, {}};
        db.exec(sql.c_str());
    }
}
} // namespace bacuri::vms::postgresql
