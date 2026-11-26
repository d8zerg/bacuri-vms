#pragma once
#include "application/control_api/api.hpp"
#include <stop_token>

namespace bacuri::vms::control::inventory
{
void track_positions(const std::stop_token&, const CameraNetworks&, const Key&);
void refresh_position(postgresql::Database&, const std::string& site, const std::string& device);
} // namespace bacuri::vms::control::inventory
