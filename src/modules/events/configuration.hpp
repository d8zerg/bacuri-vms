#pragma once
namespace bacuri::vms::postgresql
{
class ConfigurationSnapshot;
}
namespace bacuri::vms::control::events
{
void rule_configuration(postgresql::ConfigurationSnapshot& snapshot);
} // namespace bacuri::vms::control::events
