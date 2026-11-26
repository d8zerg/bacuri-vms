#include "modules/inventory/device_configuration.hpp"
#include <nlohmann/json.hpp>
namespace bacuri::vms::control
{
CameraCredentials camera_credentials(const nlohmann::json& stored)
{
    return {stored.at("username"), stored.at("password")};
}
CameraControl camera_control(const nlohmann::json& stored)
{
    CameraControl result;
    if (stored.is_null())
        return result;
    result.family = stored.value("family", result.family);
    const auto text = [&stored](const char* key) -> std::optional<std::string> {
        auto field = stored.find(key);
        if (field == stored.end())
            return {};
        return field->get<std::string>();
    };
    result.ip = text("ip");
    result.host = text("host");
    result.device_service = text("device_service");
    result.media_service = text("media_service");
    result.channel = text("channel");
    result.profile = text("profile");
    result.secondary_profile = text("secondary_profile");
    result.source = text("source");
    result.node = text("node");
    result.ptz = text("PTZ");
    result.events = text("Events");
    result.imaging = text("Imaging");
    result.device_io = text("DeviceIO");
    result.pan_space = stored.value("pan_space", std::string{});
    result.zoom_space = stored.value("zoom_space", std::string{});
    result.media2 = stored.value("media2", false);
    result.imaging_source = stored.value("imaging_source", false);
    result.ptz_profile = stored.value("ptz_profile", false);
    result.relative_move = stored.value("relative_move", false);
    result.directions = stored.value("directions", std::vector<std::string>{});
    return result;
}
CameraChannel camera_channel(const nlohmann::json& stored)
{
    const auto& identity = stored.at("onvif");
    return {camera_control(stored.at("onvif_control")),
            camera_credentials(stored.at("credentials")),
            identity.at("serial"),
            identity.at("model"),
            identity.at("firmware"),
            stored.at("main_url"),
            stored.at("secondary_url")};
}
} // namespace bacuri::vms::control
