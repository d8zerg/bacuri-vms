// Imaging settings of a camera: brightness.
#include "application/control_api/http_json.hpp"
#include "modules/inventory/onvif.hpp"
#include "modules/inventory/onvif/soap.hpp"

namespace bacuri::vms::control
{
using namespace onvif;

std::optional<BrightnessRange> camera_brightness(const CameraNetworks& networks, const CameraControl& control,
                                                 const CameraCredentials& credentials)
{
    if (!control.imaging.has_value() || !control.imaging_source)
        return {};
    auto client = control_client(networks, control, credentials);
    const auto source =
        "<timg:VideoSourceToken>" + escape(required_setting(control.channel)) + "</timg:VideoSourceToken>";
    auto options = client.call(required_setting(control.imaging), imaging, "GetOptions", source);
    auto* range = child(child(response(options, imaging, "GetOptionsResponse"), imaging, "ImagingOptions"), schema,
                        "Brightness");
    if (!range)
        return {};
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
    return BrightnessRange{read(child(range, schema, "Min")), read(child(range, schema, "Max")),
                           read(child(values, schema, "Brightness"))};
}

double apply_camera_brightness(const CameraNetworks& networks, const CameraChannel& config,
                               BrightnessChange change)
{
    const auto& control = config.control;
    const auto& credentials = config.credentials;
    auto matches = [](double value, double target) {
        return static_cast<float>(value) == static_cast<float>(target);
    };
    auto current = camera_brightness(networks, control, credentials);
    if (!current || change.requested < current->min || change.requested > current->max)
        throw Failure{ErrorCode::camera_image_unsupported};
    if (matches(current->value, change.requested))
        return current->value;
    if (!matches(current->value, change.expected))
        throw Failure{ErrorCode::revision_conflict};
    auto client = control_client(networks, control, credentials);
    client.call(required_setting(control.imaging), imaging, "SetImagingSettings",
                "<timg:VideoSourceToken>" + escape(required_setting(control.channel)) +
                    "</timg:VideoSourceToken><timg:ImagingSettings><tt:Brightness>" +
                    Json(change.requested).dump() +
                    "</tt:Brightness></timg:ImagingSettings><timg:ForcePersistence>true</timg:ForcePersistence>");
    auto actual = camera_brightness(networks, control, credentials);
    if (!actual || !matches(actual->value, change.requested))
        throw Failure{ErrorCode::camera_image_unconfirmed};
    return actual->value;
}
} // namespace bacuri::vms::control
