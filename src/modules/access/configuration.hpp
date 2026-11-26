#pragma once
namespace bacuri::vms::postgresql
{
class ConfigurationSnapshot;
}
namespace bacuri::vms::control::access
{
void account_configuration(postgresql::ConfigurationSnapshot& snapshot);
void permission_configuration(postgresql::ConfigurationSnapshot& snapshot);
} // namespace bacuri::vms::control::access
