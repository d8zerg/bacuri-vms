#include "storage/postgresql/configuration_snapshot.hpp"
#include "modules/inventory/configuration.hpp"
namespace bacuri::vms::control::inventory
{
void site_configuration(postgresql::ConfigurationSnapshot& snapshot)
{
    snapshot.section("bacuri_inventory.sites");
    snapshot.section("bacuri_inventory.node_acceleration");
}
void camera_configuration(postgresql::ConfigurationSnapshot& snapshot)
{
    snapshot.section("bacuri_inventory.observation_points");
    snapshot.section("bacuri_inventory.devices");
    snapshot.section("bacuri_inventory.channels");
    snapshot.section("bacuri_inventory.stream_profiles");
    snapshot.section("bacuri_inventory.point_bindings");
    snapshot.section("bacuri_inventory.configuration_versions");
    snapshot.section("bacuri_inventory.camera_registry");
    snapshot.section("bacuri_inventory.registration_requests");
    snapshot.section("bacuri_inventory.configuration_audit");
    snapshot.section("bacuri_inventory.camera_batch_items");
    snapshot.section("bacuri_inventory.camera_templates");
    snapshot.section("bacuri_inventory.camera_transport_audit");
    snapshot.section(
        "bacuri_inventory.configuration_progress",
        "(SELECT site_id,device_id,desired_revision,NULL::bigint AS applied_revision,"
        "'pending' AS application_state,NULL::text AS failure_code FROM bacuri_inventory.configuration_progress)");
    snapshot.section(
        "bacuri_inventory.recorder_delivery",
        "(SELECT site_id,desired_revision,0::bigint AS decided_revision FROM bacuri_inventory.recorder_delivery)");
}
} // namespace bacuri::vms::control::inventory
