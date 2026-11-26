#include "modules/access/configuration.hpp"
namespace bacuri::vms::control::access
{
void account_configuration(postgresql::ConfigurationSnapshot& snapshot)
{
    snapshot.section("bacuri_access.local_users");
    snapshot.section("bacuri_access.credential_audit");
}
void permission_configuration(postgresql::ConfigurationSnapshot& snapshot)
{
    snapshot.section("bacuri_access.site_roles");
    snapshot.section("bacuri_access.point_grants");
    snapshot.section("bacuri_access.export_grants");
    snapshot.section("bacuri_access.resource_groups");
    snapshot.section("bacuri_access.group_points");
    snapshot.section("bacuri_access.scoped_grants");
    snapshot.section("bacuri_access.privacy_policies");
    snapshot.section("bacuri_access.identity_providers");
    snapshot.section("bacuri_access.external_identities");
    snapshot.section("bacuri_access.policy_versions");
    snapshot.section("bacuri_access.access_audit");
}
} // namespace bacuri::vms::control::access
