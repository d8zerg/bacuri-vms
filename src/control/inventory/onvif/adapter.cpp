// The ONVIF implementation of the device family interface and the selection of a family adapter.
#include "control/inventory/device.hpp"
#include "control/inventory/onvif.hpp"

namespace bacuri::vms::control
{
namespace
{
class Onvif final : public DeviceFamily
{
  public:
    [[nodiscard]] std::string_view name() const override
    {
        return "onvif";
    }
    Json discover(const CameraNetworks& networks, const Key& key, const std::string& site) const override
    {
        return discover_onvif(networks, key, site);
    }
    Json inspect(const CameraNetworks& networks, const Key& key, const std::string& site,
                 const Json& body) const override
    {
        return inspect_onvif(networks, key, site, body);
    }
    Json read_selection(const Key& key, const std::string& site, const std::string& ticket) const override
    {
        return read_camera_ticket(key, site, ticket);
    }
    void verify_channel(const CameraNetworks& networks, const Json& config) const override
    {
        verify_camera_channel(networks, config);
    }
    void read_motion(const std::stop_token& stop, const CameraNetworks& networks, const Json& control,
                     const Json& credentials,
                     const std::function<void(const MotionNotice&)>& accept) const override
    {
        control::read_motion(stop, networks, control, credentials, accept);
    }
    Json read_edge_archive(const CameraNetworks& networks, const Json& control, const Json& credentials,
                           const Key& key, EdgeSelection* selected) const override
    {
        return control::read_edge_archive(networks, control, credentials, key, selected);
    }
    Json brightness(const CameraNetworks& networks, const Json& control, const Json& credentials) const override
    {
        return camera_brightness(networks, control, credentials);
    }
    double apply_brightness(const CameraNetworks& networks, const Json& config,
                            BrightnessChange change) const override
    {
        return apply_camera_brightness(networks, config, change);
    }
    Json read_ptz_position(const CameraNetworks& networks, const Json& control,
                           const Json& credentials) const override
    {
        return control::read_ptz_position(networks, control, credentials);
    }
    Json read_controls(const CameraNetworks& networks, const Json& control, const Json& credentials) const override
    {
        return read_camera_controls(networks, control, credentials);
    }
    void operate(const CameraNetworks& networks, const Json& control, const Json& credentials,
                 const CameraAction& action) const override
    {
        operate_camera(networks, control, credentials, action);
    }
    void move(const CameraNetworks& networks, const Json& control, const Json& credentials,
              const std::string& command) const override
    {
        move_onvif(networks, control, credentials, command);
    }
};
} // namespace

const DeviceFamily& device_family_named(std::string_view name)
{
    static const Onvif onvif;
    if (name == onvif.name())
        return onvif;
    throw Failure{400, "invalid_request"};
}

const DeviceFamily& device_family(const Json& control)
{
    return device_family_named(control.is_object() ? control.value("family", std::string{"onvif"})
                                                   : std::string{"onvif"});
}
} // namespace bacuri::vms::control
