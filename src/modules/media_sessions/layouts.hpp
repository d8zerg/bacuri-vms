#pragma once

#include <nlohmann/json_fwd.hpp>
#include <functional>
#include <string>
#include <vector>

namespace bacuri::vms::control::media_sessions
{
using PointVisibility = std::function<bool(const std::string&)>;

// Resolve the points of a layout in display order under the viewer's current permissions.
std::vector<std::string> layout_points(const nlohmann::json& slots, const PointVisibility& visible);
} // namespace bacuri::vms::control::media_sessions
