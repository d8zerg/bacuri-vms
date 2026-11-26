#include "application/control_api/http_json.hpp"
#include "modules/inventory/device_configuration_json.hpp"
#include "modules/inventory/orientation_json.hpp"
#include "modules/inventory/telemetry.hpp"
#include "modules/inventory/device.hpp"
#include "modules/events/operations.hpp"
#include <thread>

namespace bacuri::vms::control::inventory
{
using namespace std::chrono_literals;
void refresh_position(postgresql::Database& db, const std::string& site, const std::string& device)
{
    db.exec("UPDATE bacuri_inventory.ptz_telemetry SET next_poll=now(),rapid_until=now()+interval '5 seconds' "
            "WHERE site_id=$1 AND device_id=$2",
            {site, device});
}
namespace
{
void stop_pending_focus(postgresql::Database& db, const std::string& site, const CameraNetworks& networks,
                        const Key& key)
{
    db.exec("BEGIN");
    auto row = db.exec(R"sql(
        SELECT c.user_id,c.request_key,r.device_id,r.secret_revision,r.sealed_credentials,cfg.settings->'onvif_control'
        FROM bacuri_inventory.ptz_commands c
        JOIN bacuri_inventory.camera_registry r USING(site_id,device_id,point_id)
        JOIN bacuri_inventory.configuration_progress p USING(site_id,device_id)
        JOIN bacuri_inventory.configuration_versions cfg ON (cfg.site_id,cfg.device_id,cfg.revision)=(r.site_id,r.device_id,p.desired_revision)
        WHERE c.site_id=$1 AND c.stop_after<=clock_timestamp() AND c.focus_revision=p.desired_revision
        ORDER BY c.stop_after LIMIT 1 FOR UPDATE OF r,c SKIP LOCKED
    )sql",
                       {site});
    if (row.size()) {
        auto credentials = camera_credentials(
            Json::parse(unseal(row.get(0, 4), key, site + "/" + row.get(0, 2) + "/" + row.get(0, 3))));
        bool stopped = true;
        try {
            const auto control = camera_control(Json::parse(row.get(0, 5)));
            device_family(control).operate(networks, control, credentials, {.command = "focus_stop"});
        } catch (const Failure&) {
            stopped = false;
        }
        db.exec("UPDATE bacuri_inventory.ptz_commands SET stop_after=CASE WHEN $4::boolean THEN NULL ELSE "
                "clock_timestamp()+interval '4 seconds' END "
                "WHERE site_id=$1 AND user_id=$2 AND request_key=$3",
                {site, row.get(0), row.get(0, 1), stopped ? "true" : "false"});
    }
    db.exec("COMMIT");
}
void reconcile_commands(postgresql::Database& db, const std::string& site)
{
    db.exec("BEGIN");
    // A live dispatcher holds the camera row until its outcome is durable.
    auto orphaned = db.exec(R"sql(
        UPDATE bacuri_inventory.ptz_commands c SET status=502,failure_code='camera_ptz_unknown'
        WHERE site_id=$1 AND status IS NULL AND started_at<now()-interval '2 minutes'
          AND (NOT EXISTS (SELECT FROM bacuri_inventory.camera_registry r WHERE r.site_id=c.site_id AND r.device_id=c.device_id)
               OR EXISTS (SELECT FROM bacuri_inventory.camera_registry r WHERE r.site_id=c.site_id AND r.device_id=c.device_id
                          FOR UPDATE SKIP LOCKED))
        RETURNING point_id,user_id,command,target
    )sql",
                            {site});
    for (int i = 0; i < orphaned.size(); ++i)
        events::record_camera_control(db, site, orphaned.get(i), orphaned.get(i, 1), "unknown", orphaned.get(i, 2),
                                      orphaned.get(i, 3));
    db.exec(R"sql(
        DELETE FROM bacuri_inventory.ptz_commands c
        WHERE c.site_id=$1 AND c.status IS NOT NULL AND c.started_at<now()-interval '24 hours'
          AND (c.stop_after IS NULL OR NOT EXISTS (
            SELECT FROM bacuri_inventory.configuration_progress p
            WHERE p.site_id=c.site_id AND p.device_id=c.device_id AND p.desired_revision=c.focus_revision))
    )sql",
            {site});
    db.exec("COMMIT");
}
bool poll_one(postgresql::Database& db, const std::string& site, const CameraNetworks& networks, const Key& key)
{
    db.exec("BEGIN");
    auto row = db.exec(R"sql(
      SELECT r.device_id,p.desired_revision,cfg.settings->'onvif_control',r.secret_revision,r.sealed_credentials
      FROM bacuri_inventory.camera_registry r JOIN bacuri_inventory.configuration_progress p USING(site_id,device_id)
      JOIN bacuri_inventory.configuration_versions cfg ON (cfg.site_id,cfg.device_id,cfg.revision)=(r.site_id,r.device_id,p.desired_revision)
      LEFT JOIN bacuri_inventory.ptz_telemetry t ON (t.site_id,t.device_id)=(r.site_id,r.device_id)
      WHERE r.site_id=$1 AND r.sealed_credentials IS NOT NULL AND cfg.settings->'onvif_control' ? 'PTZ'
        AND coalesce((cfg.settings->'onvif_control'->>'ptz_profile')::boolean,true)
        AND coalesce((cfg.settings->'onvif_control'->>'status_position')::boolean,true)
        AND (t.next_poll IS NULL OR t.revision<>p.desired_revision OR t.next_poll<=now())
      ORDER BY t.next_poll NULLS FIRST,r.device_id LIMIT 1 FOR UPDATE OF r SKIP LOCKED
    )sql",
                       {site});
    if (!row.size()) {
        db.exec("COMMIT");
        return false;
    }
    auto device = row.get(0), rev = row.get(0, 1);
    // A session lock keeps a second worker from polling the same device during a priority refresh.
    if (db.exec("SELECT pg_try_advisory_lock(hashtextextended($1,180018))", {device}).get(0) != "t") {
        db.exec("COMMIT");
        return false;
    }
    db.exec(R"sql(
      INSERT INTO bacuri_inventory.ptz_telemetry AS t(site_id,device_id,revision,next_poll)
      VALUES($1,$2,$3,now()+interval '8 seconds') ON CONFLICT(site_id,device_id) DO UPDATE
      SET revision=excluded.revision,next_poll=excluded.next_poll,
          sample=CASE WHEN t.revision=excluded.revision THEN t.sample END,
          observed_at=CASE WHEN t.revision=excluded.revision THEN t.observed_at END,
          available=t.revision=excluded.revision AND t.available
    )sql",
            {site, device, rev});
    db.exec("COMMIT");
    Json sample;
    bool answered = true;
    try {
        auto credentials =
            camera_credentials(Json::parse(unseal(row.get(0, 4), key, site + "/" + device + "/" + row.get(0, 3))));
        const auto control = camera_control(Json::parse(row.get(0, 2)));
        if (const auto position = device_family(control).read_ptz_position(networks, control, credentials))
            sample = *position;
    } catch (const Failure&) {
        answered = false;
    } catch (const std::exception&) {
        answered = false;
    }
    db.exec(R"sql(
       UPDATE bacuri_inventory.ptz_telemetry SET
         sample=CASE WHEN $4::boolean THEN nullif($5::jsonb,'null') ELSE sample END,
         observed_at=CASE WHEN $4::boolean AND $5::jsonb<>'null' AND
            (sample IS NULL OR sample->>'source_time' IS DISTINCT FROM $5::jsonb->>'source_time') THEN clock_timestamp() ELSE observed_at END,
         available=$4::boolean AND $5::jsonb<>'null',
         next_poll=clock_timestamp()+CASE WHEN rapid_until>now() OR $5::jsonb->>'moving'='true' THEN interval '0.4 seconds'
            WHEN $4::boolean AND $5::jsonb='null' THEN interval '30 seconds' ELSE interval '4 seconds' END
       WHERE site_id=$1 AND device_id=$2 AND revision=$3
    )sql",
            {site, device, rev, answered ? "true" : "false", sample.dump()});
    db.exec("SELECT pg_advisory_unlock(hashtextextended($1,180018))", {device});
    return true;
}
} // namespace
void track_positions(const std::stop_token& stop, const CameraNetworks& networks, const Key& key)
{
    auto site = setting("BACURI_SITE_ID");
    while (!stop.stop_requested()) {
        try {
            postgresql::Database db;
            auto next_cleanup = std::chrono::steady_clock::now();
            auto next_stop = next_cleanup;
            while (!stop.stop_requested()) {
                if (std::chrono::steady_clock::now() >= next_cleanup) {
                    reconcile_commands(db, site);
                    next_cleanup = std::chrono::steady_clock::now() + 1min;
                }
                if (std::chrono::steady_clock::now() >= next_stop) {
                    stop_pending_focus(db, site, networks, key);
                    next_stop = std::chrono::steady_clock::now() + 500ms;
                }
                if (!poll_one(db, site, networks, key))
                    std::this_thread::sleep_for(400ms);
            }
        } catch (const std::exception&) {
            std::this_thread::sleep_for(1s);
        }
    }
}
} // namespace bacuri::vms::control::inventory
