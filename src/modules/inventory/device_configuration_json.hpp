#pragma once
#include "modules/inventory/device_configuration.hpp"
#include <nlohmann/json_fwd.hpp>
namespace bacuri::vms::control
{
CameraControl camera_control(const nlohmann::json& stored);
CameraCredentials camera_credentials(const nlohmann::json& stored);
CameraChannel camera_channel(const nlohmann::json& stored);
} // namespace bacuri::vms::control
