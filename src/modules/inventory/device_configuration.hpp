#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bacuri::vms::control
{
struct CameraCredentials {
    std::string username, password;
};
struct CameraControl {
    std::string family{"onvif"};
    std::optional<std::string> ip, host, device_service, media_service;
    std::optional<std::string> channel, profile, secondary_profile, source;
    std::string pan_space, zoom_space;
    std::optional<std::string> node, ptz, events, imaging, device_io;
    std::vector<std::string> directions;
    bool media2{}, imaging_source{}, ptz_profile{}, relative_move{};
};
struct CameraChannel {
    CameraControl control;
    CameraCredentials credentials;
    std::string serial, model, firmware, main_url, secondary_url;
};
} // namespace bacuri::vms::control
