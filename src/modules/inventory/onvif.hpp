#pragma once
#include <optional>
#include "application/control_api/api.hpp"
#include "modules/inventory/device.hpp"
#include <stop_token>
#include <functional>

namespace bacuri::vms::control
{
// The ONVIF family adapter. Core calls it through DeviceFamily.
void read_motion(const std::stop_token&, const CameraNetworks&, const CameraControl& control,
                 const CameraCredentials& credentials, const std::function<void(const MotionNotice&)>& accept);
std::vector<DiscoveredCamera> discover_onvif(const CameraNetworks& networks, const Key& key,
                                             const std::string& site);
InspectedCamera inspect_onvif(const CameraNetworks& networks, const Key& key, const std::string& site,
                              const std::string& selection_ticket, const CameraCredentials& credentials);
EdgeArchive read_edge_archive(const CameraNetworks&, const CameraControl& control,
                              const CameraCredentials& credentials, const Key& key,
                              EdgeSelection* selected = nullptr);
void verify_camera_channel(const CameraNetworks&, const CameraChannel& config);
std::optional<BrightnessRange> camera_brightness(const CameraNetworks&, const CameraControl& control,
                                                 const CameraCredentials& credentials);
double apply_camera_brightness(const CameraNetworks&, const CameraChannel& config, BrightnessChange change);
std::optional<inventory::CameraPosition> read_ptz_position(const CameraNetworks&, const CameraControl& control,
                                                           const CameraCredentials& credentials);
DeviceControls read_camera_controls(const CameraNetworks&, const CameraControl& control,
                                    const CameraCredentials& credentials);
void operate_camera(const CameraNetworks&, const CameraControl& control, const CameraCredentials& credentials,
                    const CameraAction& action);
void move_onvif(const CameraNetworks&, const CameraControl& control, const CameraCredentials& credentials,
                const std::string& command);
CameraSelection read_camera_ticket(const Key& key, const std::string& site, const std::string& ticket);
} // namespace bacuri::vms::control
