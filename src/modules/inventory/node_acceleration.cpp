#include "modules/inventory/operations.hpp"
#include "storage/postgresql/database.hpp"
#include <nlohmann/json.hpp>
namespace bacuri::vms::control::inventory
{
NodeAcceleration node_acceleration(postgresql::QuerySession& db, const std::string& site, const std::string& node,
                                   bool writing)
{
    if (writing)
        db.exec(
            "INSERT INTO bacuri_inventory.node_acceleration(site_id,node_id) VALUES($1,$2) ON CONFLICT DO NOTHING",
            {site, node});
    const char* select = "SELECT mode,to_json(gpu_tasks),revision FROM bacuri_inventory.node_acceleration WHERE "
                         "site_id=$1 AND node_id=$2";
    const std::string sql = std::string{select} + (writing ? " FOR UPDATE" : "");
    const auto rows = db.exec(sql.c_str(), {site, node});
    if (!rows.size())
        return {};
    return {{rows.get(0), nlohmann::json::parse(rows.get(0, 1)).get<std::vector<std::string>>()},
            std::stoll(rows.get(0, 2))};
}
void save_node_acceleration(postgresql::QuerySession& db, const std::string& site, const std::string& node,
                            const domain::AccelerationSettings& settings)
{
    db.exec("UPDATE bacuri_inventory.node_acceleration SET mode=$3,gpu_tasks=$4::text[],revision=revision+1 WHERE "
            "site_id=$1 AND node_id=$2",
            {site, node, settings.mode, postgresql::array_parameter(settings.gpu_tasks)});
}
} // namespace bacuri::vms::control::inventory
