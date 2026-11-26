#pragma once
#include "storage/postgresql/configuration_snapshot.hpp"
namespace bacuri::vms::control::inventory
{
void site_configuration(postgresql::ConfigurationSnapshot& snapshot);
void camera_configuration(postgresql::ConfigurationSnapshot& snapshot);
} // namespace bacuri::vms::control::inventory
