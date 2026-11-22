#include "control/maps/configuration.hpp"
namespace bacuri::vms::control::maps
{
void plan_configuration(ConfigurationSnapshot& snapshot)
{
    snapshot.section("bacuri_maps.map_catalog_settings");
    snapshot.section("bacuri_maps.floor_plans");
    snapshot.section("bacuri_maps.point_placements");
}
} // namespace bacuri::vms::control::maps
