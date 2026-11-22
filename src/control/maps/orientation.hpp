#pragma once
#include "control/api.hpp"
namespace bacuri::vms::control::maps_view
{
void calibrate(Database&, const Request&, const std::string& site);
Json sector(const Json& calibration, const Json& camera);
} // namespace bacuri::vms::control::maps_view
