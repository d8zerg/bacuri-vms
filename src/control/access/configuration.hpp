#pragma once
#include "control/configuration_snapshot.hpp"
namespace bacuri::vms::control::access
{
void account_configuration(ConfigurationSnapshot& snapshot);
void permission_configuration(ConfigurationSnapshot& snapshot);
} // namespace bacuri::vms::control::access
