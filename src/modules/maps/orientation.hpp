#pragma once
#include "application/control_api/api.hpp"
namespace bacuri::vms::control::maps_view
{
void calibrate(postgresql::Database&, const Request&, const std::string& site);
Json sector(const Json& calibration, const Json& camera);
} // namespace bacuri::vms::control::maps_view
