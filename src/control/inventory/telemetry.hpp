#pragma once
#include "control/api.hpp"
#include <stop_token>

namespace bacuri::vms::control::inventory
{
// Each point resolves to its current device. Samples belong to a specific configuration revision.
Json camera_orientations(Database&, const std::string& site, const std::vector<std::string>& points);
void track_positions(std::stop_token, const CameraNetworks&, const Key&);
void refresh_position(Database&, const std::string& site, const std::string& device);
} // namespace bacuri::vms::control::inventory
