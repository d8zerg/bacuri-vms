#include "storage/postgresql/database.hpp"
#include "modules/inventory/operations.hpp"
#include <nlohmann/json.hpp>

namespace bacuri::vms::control::inventory
{
std::map<std::string, std::int64_t> recording_bindings(postgresql::QuerySession& db, const std::string& site)
{
    auto rows = db.exec("SELECT point_id,max(binding_revision) FROM bacuri_inventory.point_bindings WHERE "
                        "site_id=$1 GROUP BY point_id",
                        {site});
    std::map<std::string, std::int64_t> result;
    for (int i = 0; i < rows.size(); ++i)
        result.emplace(rows.get(i), std::stoll(rows.get(i, 1)));
    return result;
}
bool lock_point(postgresql::Database& db, const std::string& site, const std::string& point)
{
    return db
        .exec("SELECT FROM bacuri_inventory.observation_points WHERE site_id=$1 AND point_id=$2 FOR KEY SHARE",
              {site, point})
        .size();
}
bool lock_site(postgresql::Database& db, const std::string& site)
{
    return db.exec("SELECT FROM bacuri_inventory.sites WHERE site_id=$1 FOR UPDATE", {site}).size();
}
void ensure_site(postgresql::Database& db, const std::string& site)
{
    db.exec("INSERT INTO bacuri_inventory.sites VALUES ($1,'Local site','UTC') ON CONFLICT DO NOTHING", {site});
}
bool has_binding(postgresql::Database& db, const std::string& site, const std::string& point,
                 const std::string& revision, bool current)
{
    return db
        .exec(
            "SELECT FROM bacuri_inventory.point_bindings WHERE site_id=$1 AND point_id=$2 AND binding_revision=$3 "
            "AND (NOT $4::boolean OR binding_revision=(SELECT max(binding_revision) FROM "
            "bacuri_inventory.point_bindings "
            "WHERE site_id=$1 AND point_id=$2))",
            {site, point, revision, current ? "true" : "false"})
        .size();
}
bool lock_camera_revision(postgresql::Database& db, const std::string& site, const std::string& point,
                          const std::string& device, const std::string& revision)
{
    return db
        .exec("SELECT FROM bacuri_inventory.camera_registry r JOIN bacuri_inventory.configuration_progress p "
              "USING(site_id,device_id) "
              "WHERE r.site_id=$1 AND r.point_id=$2 AND r.device_id=$3 AND p.desired_revision=$4 FOR SHARE OF r,p",
              {site, point, device, revision})
        .size();
}

bool site_exists(postgresql::Database& db, const std::string& site)
{
    return db.exec("SELECT FROM bacuri_inventory.sites WHERE site_id=$1", {site}).size();
}

bool point_exists(postgresql::Database& db, const std::string& site, const std::string& point)
{
    return db
        .exec("SELECT FROM bacuri_inventory.observation_points WHERE site_id=$1 AND point_id=$2", {site, point})
        .size();
}

nlohmann::json camera_orientations(postgresql::Database& db, const std::string& site,
                                   const std::vector<std::string>& points)
{
    using Json = nlohmann::json;
    Json result = Json::object();
    if (points.empty())
        return result;
    auto rows = db.exec(R"sql(
      SELECT r.point_id,r.device_id,coalesce(cfg.settings->'onvif_control' ? 'PTZ' AND
        coalesce((cfg.settings->'onvif_control'->>'ptz_profile')::boolean,true),false),
        t.sample,to_char(t.observed_at AT TIME ZONE 'UTC','YYYY-MM-DD"T"HH24:MI:SS.MS"Z"'),
        t.available AND t.observed_at > now()-interval '15 seconds',cfg.settings->'onvif_control'->>'profile',p.desired_revision
      FROM bacuri_inventory.camera_registry r
      JOIN bacuri_inventory.configuration_progress p USING(site_id,device_id)
      JOIN bacuri_inventory.configuration_versions cfg ON (cfg.site_id,cfg.device_id,cfg.revision)=(r.site_id,r.device_id,p.desired_revision)
      LEFT JOIN bacuri_inventory.ptz_telemetry t ON (t.site_id,t.device_id,t.revision)=(r.site_id,r.device_id,p.desired_revision)
      WHERE r.site_id=$1 AND r.point_id=ANY($2::uuid[])
    )sql",
                        {site, postgresql::array_parameter(points)});
    for (int i = 0; i < rows.size(); ++i) {
        Json device{{"device_id", rows.get(i, 1)},
                    {"ptz", rows.get(i, 2) == "t"},
                    {"profile", rows.get(i, 6)},
                    {"revision", rows.get(i, 7)}};
        if (!rows.get(i, 3).empty()) {
            device.update(Json::parse(rows.get(i, 3)));
            device["observed_at"] = rows.get(i, 4);
            device["fresh"] = rows.get(i, 5) == "t";
        }
        result[rows.get(i)] = std::move(device);
    }
    return result;
}
} // namespace bacuri::vms::control::inventory
