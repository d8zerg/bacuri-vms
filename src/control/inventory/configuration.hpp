#pragma once
#include "control/configuration_snapshot.hpp"
namespace bacuri::vms::control::inventory
{
void site_configuration(ConfigurationSnapshot& snapshot);
void camera_configuration(ConfigurationSnapshot& snapshot);
} // namespace bacuri::vms::control::inventory
