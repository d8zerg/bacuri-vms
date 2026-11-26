// Imaging settings of a camera: brightness.
#include "modules/inventory/onvif.hpp"
#include "modules/inventory/onvif/soap.hpp"

namespace bacuri::vms::control
{
using namespace onvif;

Json camera_brightness(const CameraNetworks& networks, const CameraControl& control,
                       const CameraCredentials& credentials)
{
    if (!control.imaging.has_value() || !control.imaging_source)
        return nullptr;
    auto client = control_client(networks, control, credentials);
    const auto source =
        "<timg:VideoSourceToken>" + escape(required_setting(control.channel)) + "</timg:VideoSourceToken>";
    auto options = client.call(required_setting(control.imaging), imaging, "GetOptions", source);
    auto* range = child(child(response(options, imaging, "GetOptionsResponse"), imaging, "ImagingOptions"), schema,
                        "Brightness");
    if (!range)
        return nullptr;
    auto read = [](xmlNode* n) {
        auto raw = value(n);
        char* end = nullptr;
        auto v = g_ascii_strtod(raw.c_str(), &end);
        if (*end || !std::isfinite(v))
            throw Failure{ErrorCode::camera_protocol_error};
        return v;
    };
    auto settings = client.call(required_setting(control.imaging), imaging, "GetImagingSettings", source);
    auto* values = child(response(settings, imaging, "GetImagingSettingsResponse"), imaging, "ImagingSettings");
    return {{"min", read(child(range, schema, "Min"))},
            {"max", read(child(range, schema, "Max"))},
            {"value", read(child(values, schema, "Brightness"))}};
}

double apply_camera_brightness(const CameraNetworks& networks, const CameraChannel& config,
                               BrightnessChange change)
{
    const auto& control = config.control;
    const auto& credentials = config.credentials;
    auto matches = [](const Json& value, double target) {
        return value.get<float>() == static_cast<float>(target);
    };
    auto current = camera_brightness(networks, control, credentials);
    if (current.is_null() || change.requested < current.at("min").get<double>() ||
        change.requested > current.at("max").get<double>())
        throw Failure{ErrorCode::camera_image_unsupported};
    if (matches(current.at("value"), change.requested))
        return current.at("value").get<double>();
    if (!matches(current.at("value"), change.expected))
        throw Failure{ErrorCode::revision_conflict};
    auto client = control_client(networks, control, credentials);
    client.call(required_setting(control.imaging), imaging, "SetImagingSettings",
                "<timg:VideoSourceToken>" + escape(required_setting(control.channel)) +
                    "</timg:VideoSourceToken><timg:ImagingSettings><tt:Brightness>" +
                    Json(change.requested).dump() +
                    "</tt:Brightness></timg:ImagingSettings><timg:ForcePersistence>true</timg:ForcePersistence>");
    auto actual = camera_brightness(networks, control, credentials);
    if (actual.is_null() || !matches(actual.at("value"), change.requested))
        throw Failure{ErrorCode::camera_image_unconfirmed};
    return actual.at("value").get<double>();
}
} // namespace bacuri::vms::control
