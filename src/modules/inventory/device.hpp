#pragma once
#include "application/control_api/api.hpp"

#include <functional>
#include <memory>
#include <stop_token>
#include <string_view>

namespace bacuri::vms::control
{
// A motion or input event reported by a camera.
struct MotionNotice {
    Json source;
    std::int64_t utc, uncertainty;
    std::string epoch;
    std::string input;
    bool active{};
    std::string topic{"RuleEngine/CellMotionDetector/Motion"};
    Json metadata = Json::object();
};
// A recording on the camera chosen for replay.
struct EdgeSelection {
    std::string id, uri;
    std::int64_t offset_us{}, uncertainty_us{};
};
struct BrightnessChange {
    double expected, requested;
};
struct CameraAction {
    std::string command, target{}, mode{};
    double amount{};
};

// The operations Core needs from one family of devices. A family adapter is a pluggable module (ADR-018): it
// translates these operations into the protocol of the device. `control` and `credentials` are the stored
// connection settings of the camera.
class DeviceFamily
{
  public:
    DeviceFamily() = default;
    DeviceFamily(const DeviceFamily&) = delete;
    DeviceFamily& operator=(const DeviceFamily&) = delete;
    virtual ~DeviceFamily() = default;

    [[nodiscard]] virtual std::string_view name() const = 0;
    // Devices of the family reachable on the camera networks, each with a selection ticket.
    virtual Json discover(const CameraNetworks&, const Key&, const std::string& site) const = 0;
    // Profiles and services of a selected device, read with the given credentials.
    virtual Json inspect(const CameraNetworks&, const Key&, const std::string& site, const Json& body) const = 0;
    virtual Json read_selection(const Key&, const std::string& site, const std::string& ticket) const = 0;
    virtual void verify_channel(const CameraNetworks&, const Json& config) const = 0;
    virtual void read_motion(const std::stop_token&, const CameraNetworks&, const Json& control,
                             const Json& credentials,
                             const std::function<void(const MotionNotice&)>& accept) const = 0;
    virtual Json read_edge_archive(const CameraNetworks&, const Json& control, const Json& credentials,
                                   const Key& key, EdgeSelection* selected) const = 0;
    virtual Json brightness(const CameraNetworks&, const Json& control, const Json& credentials) const = 0;
    virtual double apply_brightness(const CameraNetworks&, const Json& config, BrightnessChange change) const = 0;
    virtual Json read_ptz_position(const CameraNetworks&, const Json& control, const Json& credentials) const = 0;
    virtual Json read_controls(const CameraNetworks&, const Json& control, const Json& credentials) const = 0;
    virtual void operate(const CameraNetworks&, const Json& control, const Json& credentials,
                         const CameraAction& action) const = 0;
    virtual void move(const CameraNetworks&, const Json& control, const Json& credentials,
                      const std::string& command) const = 0;
};

// The adapter of the family named in the control settings of a camera. Settings without a family belong to ONVIF.
const DeviceFamily& device_family(const Json& control);
// The adapter of a family by its name, such as "onvif" for discovery.
const DeviceFamily& device_family_named(std::string_view name);
} // namespace bacuri::vms::control
