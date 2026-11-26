#pragma once
#include "storage/postgresql/configuration_snapshot.hpp"
namespace bacuri::vms::control::access
{
void account_configuration(postgresql::ConfigurationSnapshot& snapshot);
void permission_configuration(postgresql::ConfigurationSnapshot& snapshot);
} // namespace bacuri::vms::control::access
