#pragma once
#include <optional>
#include <map>
#include "modules/inventory/registration_types.hpp"
#include "modules/inventory/device_configuration.hpp"
#include "modules/inventory/operations.hpp"
#include "application/control_api/api.hpp"

#include <functional>
#include <memory>
#include <stop_token>
#include <string_view>

namespace bacuri::vms::control
{
// A motion or input event reported by a camera.
struct MotionNotice {
    std::map<std::string, std::string> source;
    std::int64_t utc, uncertainty;
    std::string epoch;
    std::string input;
    bool active{};
    std::string topic{"RuleEngine/CellMotionDetector/Motion"};
    std::map<std::string, std::string> metadata{};
};
// A recording on the camera chosen for replay.
struct EdgeSelection {
    std::string id, uri;
    std::int64_t offset_us{}, uncertainty_us{};
};
struct BrightnessRange {
    double min{}, max{}, value{};
};
struct DiscoveredCamera {
    std::string endpoint_id, device_service, selection;
};
struct CameraProfile {
    std::string token, name, url, channel, codec;
    std::optional<unsigned> width, height;
};
struct InspectedCamera {
    std::string endpoint_id, manufacturer, model, firmware, serial, selection;
    bool ptz{}, events{};
    std::vector<CameraProfile> profiles;
};
struct DeviceCommand {
    std::string command, target, label, mode;
    double amount{};
    std::optional<int> duration;
};
struct DeviceControls {
    std::vector<DeviceCommand> actions;
    std::vector<std::string> inputs;
};
struct EdgeTrack {
    std::string kind;
    std::optional<std::pair<std::int64_t, std::int64_t>> bounds;
};
struct EdgeRecording {
    std::string id, name;
    std::vector<EdgeTrack> tracks;
};
struct EdgeArchive {
    bool supported{};
    std::vector<EdgeRecording> recordings;
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
    virtual std::vector<DiscoveredCamera> discover(const CameraNetworks&, const Key&,
                                                   const std::string& site) const = 0;
    // Profiles and services of a selected device, read with the given credentials.
    virtual InspectedCamera inspect(const CameraNetworks&, const Key&, const std::string& site,
                                    const std::string& selection_ticket,
                                    const CameraCredentials& credentials) const = 0;
    virtual CameraSelection read_selection(const Key&, const std::string& site,
                                           const std::string& ticket) const = 0;
    virtual void verify_channel(const CameraNetworks&, const CameraChannel& config) const = 0;
    virtual void read_motion(const std::stop_token&, const CameraNetworks&, const CameraControl& control,
                             const CameraCredentials& credentials,
                             const std::function<void(const MotionNotice&)>& accept) const = 0;
    virtual EdgeArchive read_edge_archive(const CameraNetworks&, const CameraControl& control,
                                          const CameraCredentials& credentials, const Key& key,
                                          EdgeSelection* selected) const = 0;
    virtual std::optional<BrightnessRange> brightness(const CameraNetworks&, const CameraControl& control,
                                                      const CameraCredentials& credentials) const = 0;
    virtual double apply_brightness(const CameraNetworks&, const CameraChannel& config,
                                    BrightnessChange change) const = 0;
    virtual std::optional<inventory::CameraPosition>
    read_ptz_position(const CameraNetworks&, const CameraControl& control,
                      const CameraCredentials& credentials) const = 0;
    virtual DeviceControls read_controls(const CameraNetworks&, const CameraControl& control,
                                         const CameraCredentials& credentials) const = 0;
    virtual void operate(const CameraNetworks&, const CameraControl& control, const CameraCredentials& credentials,
                         const CameraAction& action) const = 0;
    virtual void move(const CameraNetworks&, const CameraControl& control, const CameraCredentials& credentials,
                      const std::string& command) const = 0;
};

// The adapter of the family named in the control settings of a camera. Settings without a family belong to ONVIF.
const DeviceFamily& device_family(const CameraControl& control);
// The adapter of a family by its name, such as "onvif" for discovery.
const DeviceFamily& device_family_named(std::string_view name);
} // namespace bacuri::vms::control
