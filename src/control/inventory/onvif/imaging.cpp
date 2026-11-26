// Imaging settings of a camera: brightness.
#include "control/inventory/onvif.hpp"
#include "control/inventory/onvif/soap.hpp"

namespace bacuri::vms::control
{
using namespace onvif;

Json camera_brightness(const CameraNetworks& networks, const Json& control, const Json& credentials)
{
    if (!control.contains("Imaging") || !control.value("imaging_source", false))
        return nullptr;
    auto client = control_client(networks, control, credentials);
    const auto source = "<timg:VideoSourceToken>" + escape(control.at("channel").get_ref<const std::string&>()) +
                        "</timg:VideoSourceToken>";
    auto options = client.call(control.at("Imaging"), imaging, "GetOptions", source);
    auto* range = child(child(response(options, imaging, "GetOptionsResponse"), imaging, "ImagingOptions"), schema,
                        "Brightness");
    if (!range)
        return nullptr;
    auto read = [](xmlNode* n) {
        auto raw = value(n);
        char* end = nullptr;
        auto v = g_ascii_strtod(raw.c_str(), &end);
        if (*end || !std::isfinite(v))
            throw Failure{502, "camera_protocol_error"};
        return v;
    };
    auto settings = client.call(control.at("Imaging"), imaging, "GetImagingSettings", source);
    auto* values = child(response(settings, imaging, "GetImagingSettingsResponse"), imaging, "ImagingSettings");
    return {{"min", read(child(range, schema, "Min"))},
            {"max", read(child(range, schema, "Max"))},
            {"value", read(child(values, schema, "Brightness"))}};
}

double apply_camera_brightness(const CameraNetworks& networks, const Json& config, BrightnessChange change)
{
    const auto& control = config.at("onvif_control");
    const auto& credentials = config.at("credentials");
    auto matches = [](const Json& value, double target) {
        return value.get<float>() == static_cast<float>(target);
    };
    auto current = camera_brightness(networks, control, credentials);
    if (current.is_null() || change.requested < current.at("min").get<double>() ||
        change.requested > current.at("max").get<double>())
        throw Failure{422, "camera_image_unsupported"};
    if (matches(current.at("value"), change.requested))
        return current.at("value").get<double>();
    if (!matches(current.at("value"), change.expected))
        throw Failure{409, "revision_conflict"};
    auto client = control_client(networks, control, credentials);
    client.call(control.at("Imaging"), imaging, "SetImagingSettings",
                "<timg:VideoSourceToken>" + escape(control.at("channel").get_ref<const std::string&>()) +
                    "</timg:VideoSourceToken><timg:ImagingSettings><tt:Brightness>" +
                    Json(change.requested).dump() +
                    "</tt:Brightness></timg:ImagingSettings><timg:ForcePersistence>true</timg:ForcePersistence>");
    auto actual = camera_brightness(networks, control, credentials);
    if (actual.is_null() || !matches(actual.at("value"), change.requested))
        throw Failure{502, "camera_image_unconfirmed"};
    return actual.at("value").get<double>();
}
} // namespace bacuri::vms::control
