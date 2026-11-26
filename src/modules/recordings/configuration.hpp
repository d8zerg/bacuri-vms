#pragma once
#include "storage/postgresql/configuration_snapshot.hpp"
namespace bacuri::vms::control::recordings
{
void retention_configuration(postgresql::ConfigurationSnapshot& snapshot);
}
