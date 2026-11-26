#include "modules/maps/operations.hpp"
#include "storage/postgresql/database.hpp"

namespace bacuri::vms::control
{
bool point_on_plan(postgresql::QuerySession& db, const std::string& site, const std::string& point,
                   const std::string& plan)
{
    return db.exec("SELECT FROM bacuri_maps.point_placements WHERE site_id=$1 AND point_id=$2 AND plan_id=$3",
                   {site, point, plan})
               .size() != 0;
}
} // namespace bacuri::vms::control
