#include "control/database.hpp"
#include "control/inventory/operations.hpp"

namespace bacuri::vms::control::inventory
{
std::map<std::string, std::int64_t> recording_bindings(Database& db, const std::string& site)
{
    auto rows = db.exec("SELECT point_id,max(binding_revision) FROM bacuri_inventory.point_bindings WHERE "
                        "site_id=$1 GROUP BY point_id",
                        {site});
    std::map<std::string, std::int64_t> result;
    for (int i = 0; i < rows.size(); ++i)
        result.emplace(rows.get(i), std::stoll(rows.get(i, 1)));
    return result;
}
bool lock_point(Database& db, const std::string& site, const std::string& point)
{
    return db
        .exec("SELECT FROM bacuri_inventory.observation_points WHERE site_id=$1 AND point_id=$2 FOR KEY SHARE",
              {site, point})
        .size();
}
bool lock_site(Database& db, const std::string& site)
{
    return db.exec("SELECT FROM bacuri_inventory.sites WHERE site_id=$1 FOR UPDATE", {site}).size();
}
void ensure_site(Database& db, const std::string& site)
{
    db.exec("INSERT INTO bacuri_inventory.sites VALUES ($1,'Local site','UTC') ON CONFLICT DO NOTHING", {site});
}
bool has_binding(Database& db, const std::string& site, const std::string& point, const std::string& revision,
                 bool current)
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
bool lock_camera_revision(Database& db, const std::string& site, const std::string& point,
                          const std::string& device, const std::string& revision)
{
    return db
        .exec("SELECT FROM bacuri_inventory.camera_registry r JOIN bacuri_inventory.configuration_progress p "
              "USING(site_id,device_id) "
              "WHERE r.site_id=$1 AND r.point_id=$2 AND r.device_id=$3 AND p.desired_revision=$4 FOR SHARE OF r,p",
              {site, point, device, revision})
        .size();
}

bool site_exists(Database& db, const std::string& site)
{
    return db.exec("SELECT FROM bacuri_inventory.sites WHERE site_id=$1", {site}).size();
}

bool point_exists(Database& db, const std::string& site, const std::string& point)
{
    return db
        .exec("SELECT FROM bacuri_inventory.observation_points WHERE site_id=$1 AND point_id=$2", {site, point})
        .size();
}
} // namespace bacuri::vms::control::inventory
