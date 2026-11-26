#pragma once
namespace bacuri::vms::postgresql
{
class ConfigurationSnapshot;
}
namespace bacuri::vms::control::inventory
{
void site_configuration(postgresql::ConfigurationSnapshot& snapshot);
void camera_configuration(postgresql::ConfigurationSnapshot& snapshot);
} // namespace bacuri::vms::control::inventory
