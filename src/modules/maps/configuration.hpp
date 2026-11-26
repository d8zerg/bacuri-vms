#pragma once
#include "storage/postgresql/configuration_snapshot.hpp"
namespace bacuri::vms::control::maps
{
void plan_configuration(postgresql::ConfigurationSnapshot& snapshot);
}
