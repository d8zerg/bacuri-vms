#pragma once
namespace bacuri::vms::postgresql
{
class ConfigurationSnapshot;
}
namespace bacuri::vms::control::maps
{
void plan_configuration(postgresql::ConfigurationSnapshot& snapshot);
}
