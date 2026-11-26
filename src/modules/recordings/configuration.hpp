#pragma once
namespace bacuri::vms::postgresql
{
class ConfigurationSnapshot;
}
namespace bacuri::vms::control::recordings
{
void retention_configuration(postgresql::ConfigurationSnapshot& snapshot);
}
