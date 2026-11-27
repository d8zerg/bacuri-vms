#pragma once
#include <nlohmann/json_fwd.hpp>
#include "domain/acceleration.hpp"

namespace bacuri::vms::acceleration
{
using Settings = domain::AccelerationSettings;
// Validate the mode and return the selected tasks in priority order.
bool normalize(Settings& settings);
// Candidate assignments for one reference unit of each selected task.
nlohmann::json evaluate(const Settings& settings, const nlohmann::json& probe);
} // namespace bacuri::vms::acceleration
