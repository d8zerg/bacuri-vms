#include "control/access/configuration.hpp"
namespace bacuri::vms::control::access
{
void account_configuration(ConfigurationSnapshot& snapshot)
{
    snapshot.section("bacuri_access.local_users");
    snapshot.section("bacuri_access.credential_audit");
}
void permission_configuration(ConfigurationSnapshot& snapshot)
{
    snapshot.section("bacuri_access.site_roles");
    snapshot.section("bacuri_access.point_grants");
    snapshot.section("bacuri_access.export_grants");
}
} // namespace bacuri::vms::control::access
