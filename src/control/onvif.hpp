#pragma once
#include "control/api.hpp"
#include <stop_token>
#include <functional>

namespace bacuri::vms::control
{
struct MotionNotice {
    Json source;
    std::int64_t utc, uncertainty;
    std::string epoch;
    std::string input;
    bool active{};
    std::string topic{"RuleEngine/CellMotionDetector/Motion"};
    Json metadata = Json::object();
};
void read_motion(std::stop_token, const CameraNetworks&, const Json& control, const Json& credentials,
                 const std::function<void(const MotionNotice&)>& accept);
Json discover_onvif(const CameraNetworks& networks, const Key& key, const std::string& site);
Json inspect_onvif(const CameraNetworks& networks, const Key& key, const std::string& site, const Json& body);
struct EdgeSelection {
    std::string id, uri;
    std::int64_t offset_us{}, uncertainty_us{};
};
Json read_edge_archive(const CameraNetworks&, const Json& control, const Json& credentials, const Key& key,
                       EdgeSelection* selected = nullptr);
void verify_camera_channel(const CameraNetworks&, const Json& config);
Json camera_brightness(const CameraNetworks&, const Json& control, const Json& credentials);
struct BrightnessChange {
    double expected, requested;
};
double apply_camera_brightness(const CameraNetworks&, const Json& config, BrightnessChange change);
Json read_ptz_position(const CameraNetworks&, const Json& control, const Json& credentials);
Json read_camera_controls(const CameraNetworks&, const Json& control, const Json& credentials);
struct CameraAction {
    std::string command, target{}, mode{};
    double amount{};
};
void operate_camera(const CameraNetworks&, const Json& control, const Json& credentials,
                    const CameraAction& action);
void move_onvif(const CameraNetworks&, const Json& control, const Json& credentials, const std::string& command);
Json read_camera_ticket(const Key& key, const std::string& site, const std::string& ticket);
} // namespace bacuri::vms::control
