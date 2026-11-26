#pragma once
#include <optional>
#include "modules/inventory/device_configuration.hpp"
#include <map>
namespace bacuri::vms::control
{
struct CameraIdentification {
    std::string endpoint_id, manufacturer, model, firmware, serial;
    bool ptz{}, events{};
    std::optional<std::string> channel_token;
};
struct RegisteredCamera {
    std::string id, point_id, name, main_url, secondary_url, desired_revision, binding_revision, state;
    bool has_credentials{};
    std::optional<std::string> applied_revision, apply_failure;
    std::optional<CameraIdentification> identification;
};
struct OnvifServices {
    std::optional<std::string> family, ip, host, device_service, media_service, ptz, events, imaging, device_io;
    std::optional<bool> media2, status_position;
};
struct SelectedProfile {
    std::optional<std::string> profile, channel, source, node, pan_space, zoom_space, stream_url, codec;
    std::optional<bool> relative_move, ptz_profile, imaging_source, audio_input, audio_output;
    std::optional<std::int64_t> pixels;
    std::optional<std::vector<std::string>> directions;
};
struct CameraSelection {
    CameraIdentification identity;
    OnvifServices services;
    std::map<std::string, SelectedProfile> profiles;
};
struct ChannelSettings {
    OnvifServices services;
    SelectedProfile primary;
    std::optional<std::string> secondary_profile;
};
struct SelectedChannel {
    CameraIdentification identity;
    ChannelSettings control;
    std::string main_url, secondary_url;
    std::map<std::string, std::string> profile_channels, stream_channels;
};
struct CameraRegistration {
    std::string name;
    std::optional<std::string> point_id, replace_device_id;
};
struct CameraVersion {
    std::string main_url, secondary_url;
    std::optional<CameraIdentification> camera;
    std::optional<ChannelSettings> control;
    bool update_credentials{};
    std::optional<CameraCredentials> credentials;
};
} // namespace bacuri::vms::control
