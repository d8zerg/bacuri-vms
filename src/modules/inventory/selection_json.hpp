#pragma once
#include "modules/inventory/device_configuration_json.hpp"
#include "modules/inventory/registered_camera_json.hpp"
namespace bacuri::vms::control
{
template <class T>
void read_selection_field(const nlohmann::json& object, const char* key, std::optional<T>& value)
{
    const auto field = object.find(key);
    if (field != object.end() && !field->is_null())
        value = field->get<T>();
}
template <class T>
void write_selection_field(nlohmann::json& object, const char* key, const std::optional<T>& value)
{
    if (value)
        object[key] = *value;
}
inline void from_json(const nlohmann::json& j, OnvifServices& v)
{
    v = {};
    read_selection_field(j, "family", v.family);
    read_selection_field(j, "ip", v.ip);
    read_selection_field(j, "host", v.host);
    read_selection_field(j, "device_service", v.device_service);
    read_selection_field(j, "media_service", v.media_service);
    read_selection_field(j, "PTZ", v.ptz);
    read_selection_field(j, "Events", v.events);
    read_selection_field(j, "Imaging", v.imaging);
    read_selection_field(j, "DeviceIO", v.device_io);
    read_selection_field(j, "media2", v.media2);
    read_selection_field(j, "status_position", v.status_position);
}
inline void to_json(nlohmann::json& j, const OnvifServices& v)
{
    j = nlohmann::json::object();
    write_selection_field(j, "family", v.family);
    write_selection_field(j, "ip", v.ip);
    write_selection_field(j, "host", v.host);
    write_selection_field(j, "device_service", v.device_service);
    write_selection_field(j, "media_service", v.media_service);
    write_selection_field(j, "PTZ", v.ptz);
    write_selection_field(j, "Events", v.events);
    write_selection_field(j, "Imaging", v.imaging);
    write_selection_field(j, "DeviceIO", v.device_io);
    write_selection_field(j, "media2", v.media2);
    write_selection_field(j, "status_position", v.status_position);
}
inline void from_json(const nlohmann::json& j, SelectedProfile& v)
{
    v = {};
    read_selection_field(j, "profile", v.profile);
    read_selection_field(j, "channel", v.channel);
    read_selection_field(j, "source", v.source);
    read_selection_field(j, "node", v.node);
    read_selection_field(j, "pan_space", v.pan_space);
    read_selection_field(j, "zoom_space", v.zoom_space);
    read_selection_field(j, "stream_url", v.stream_url);
    read_selection_field(j, "codec", v.codec);
    read_selection_field(j, "relative_move", v.relative_move);
    read_selection_field(j, "ptz_profile", v.ptz_profile);
    read_selection_field(j, "imaging_source", v.imaging_source);
    read_selection_field(j, "audio_input", v.audio_input);
    read_selection_field(j, "audio_output", v.audio_output);
    read_selection_field(j, "pixels", v.pixels);
    read_selection_field(j, "directions", v.directions);
}
inline void to_json(nlohmann::json& j, const SelectedProfile& v)
{
    j = nlohmann::json::object();
    write_selection_field(j, "profile", v.profile);
    write_selection_field(j, "channel", v.channel);
    write_selection_field(j, "source", v.source);
    write_selection_field(j, "node", v.node);
    write_selection_field(j, "pan_space", v.pan_space);
    write_selection_field(j, "zoom_space", v.zoom_space);
    write_selection_field(j, "stream_url", v.stream_url);
    write_selection_field(j, "codec", v.codec);
    write_selection_field(j, "relative_move", v.relative_move);
    write_selection_field(j, "ptz_profile", v.ptz_profile);
    write_selection_field(j, "imaging_source", v.imaging_source);
    write_selection_field(j, "audio_input", v.audio_input);
    write_selection_field(j, "audio_output", v.audio_output);
    write_selection_field(j, "pixels", v.pixels);
    write_selection_field(j, "directions", v.directions);
}
inline void from_json(const nlohmann::json& j, ChannelSettings& s)
{
    s = {j.get<OnvifServices>(), j.get<SelectedProfile>(), {}};
    if (j.contains("secondary_profile"))
        s.secondary_profile = j.at("secondary_profile").get<std::string>();
}
inline void to_json(nlohmann::json& j, const ChannelSettings& s)
{
    j = s.services;
    j.update(nlohmann::json(s.primary));
    if (s.secondary_profile)
        j["secondary_profile"] = *s.secondary_profile;
}
inline void from_json(const nlohmann::json& j, CameraSelection& s)
{
    s = {j.get<CameraIdentification>(), j.at("control").get<OnvifServices>(),
         j.at("profiles").get<std::map<std::string, SelectedProfile>>()};
}
inline void to_json(nlohmann::json& j, const CameraSelection& s)
{
    j = s.identity;
    j["control"] = s.services;
    j["profiles"] = s.profiles;
}
inline void from_json(const nlohmann::json& j, SelectedChannel& s)
{
    s = {j.at("onvif"),         j.at("onvif_control"),    j.at("main_url"),
         j.at("secondary_url"), j.at("profile_channels"), j.at("stream_channels")};
}
inline void to_json(nlohmann::json& j, const SelectedChannel& s)
{
    j = {{"onvif", s.identity},
         {"onvif_control", s.control},
         {"main_url", s.main_url},
         {"secondary_url", s.secondary_url},
         {"profile_channels", s.profile_channels},
         {"stream_channels", s.stream_channels}};
}
inline void from_json(const nlohmann::json& j, CameraRegistration& r)
{
    r = {j.at("name"), {}, {}};
    if (j.contains("point_id"))
        r.point_id = j.at("point_id").get<std::string>();
    if (j.contains("replace_device_id"))
        r.replace_device_id = j.at("replace_device_id").get<std::string>();
}
inline void from_json(const nlohmann::json& j, CameraVersion& v)
{
    v = {j.at("main_url"), j.at("secondary_url"), {}, {}, j.contains("credentials"), {}};
    if (j.contains("onvif"))
        v.camera = j.at("onvif").get<CameraIdentification>();
    if (j.contains("onvif_control") && !j.at("onvif_control").is_null())
        v.control = j.at("onvif_control").get<ChannelSettings>();
    if (v.update_credentials && !j.at("credentials").is_null())
        v.credentials = camera_credentials(j.at("credentials"));
}
} // namespace bacuri::vms::control
