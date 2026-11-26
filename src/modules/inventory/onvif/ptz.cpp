// PTZ position, camera controls and movement commands.
#include "modules/inventory/onvif.hpp"
#include "modules/inventory/onvif/soap.hpp"

namespace bacuri::vms::control
{
using namespace onvif;

Json read_ptz_position(const CameraNetworks& networks, const CameraControl& control,
                       const CameraCredentials& credentials)
{
    auto client = control_client(networks, control, credentials);
    auto doc =
        client.call(required_setting(control.ptz), ptz, "GetStatus",
                    "<tptz:ProfileToken>" + escape(required_setting(control.profile)) + "</tptz:ProfileToken>");
    auto* status = child(response(doc, ptz, "GetStatusResponse"), ptz, "PTZStatus");
    if (!status || (child(status, schema, "Error") && child(status, schema, "Error")->children))
        throw Failure{ErrorCode::camera_protocol_error};
    auto* position = child(status, schema, "Position");
    auto* pan = child(position, schema, "PanTilt");
    auto number = [](xmlNode* node, const char* key) -> std::optional<double> {
        auto text = attribute(node, key);
        if (text.empty())
            return {};
        char* end{};
        double n = g_ascii_strtod(text.c_str(), &end);
        if (*end || !std::isfinite(n))
            throw Failure{ErrorCode::camera_protocol_error};
        return n;
    };
    auto space = attribute(pan, "space");
    if (space.empty())
        space = control.pan_space;
    auto x = number(pan, "x");
    if (x && std::abs(*x) > 1e9)
        throw Failure{ErrorCode::camera_protocol_error};
    if (!x || space.empty())
        return nullptr;
    Json sample{{"pan", *x}, {"space", space}};
    if (auto tilt = number(pan, "y"))
        sample["tilt"] = *tilt;
    auto* motion = child(status, schema, "MoveStatus");
    bool moving = false, idle = false, unknown = false;
    for (const auto* axis : {"PanTilt", "Zoom"}) {
        if (auto* state = child(motion, schema, axis)) {
            const auto reported = value(state);
            moving |= reported == "MOVING";
            idle |= reported == "IDLE";
            unknown |= reported != "IDLE" && reported != "MOVING";
        }
    }
    if (moving || (idle && !unknown))
        sample["moving"] = moving;
    if (auto fov = number(child(status, schema, "FieldOfView"), "hfov"); fov && *fov > 0 && *fov < 180)
        sample["hfov"] = *fov;
    auto* zoom = child(position, schema, "Zoom");
    auto zspace = attribute(zoom, "space");
    if (zspace.empty())
        zspace = control.zoom_space;
    if (auto z = number(zoom, "x");
        z && *z >= 0 && *z <= 1 && zspace == "http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace")
        sample["zoom"] = *z;
    const auto stamp = value(child(status, schema, "UtcTime"));
    Timestamp parsed;
    if (!ProtoTime::FromString(stamp, &parsed))
        throw Failure{ErrorCode::camera_protocol_error};
    sample["source_time"] = ProtoTime::ToString(parsed);
    return sample;
}

Json read_camera_controls(const CameraNetworks& networks, const CameraControl& control,
                          const CameraCredentials& credentials)
{
    auto client = control_client(networks, control, credentials);
    client.deadline = Clock::now() + 12s;
    Json actions = Json::array(), inputs = Json::array();
    auto add = [&](std::string_view command, const std::string& target = {},
                   const std::string& title = {}) -> Json& {
        return actions.emplace_back(
            Json{{"command", command}, {"target", target}, {"label", title.empty() ? target : title}});
    };
    auto optional = [&](const std::optional<std::string>& service, std::string_view ns, const char* op,
                        const std::string& params, const auto& consume, const AddressingHeaders& headers = {}) {
        if (!service)
            return;
        try {
            auto doc = client.call(*service, ns, op, params, headers);
            consume(response(doc, ns, (std::string(op) + "Response").c_str()));
        } catch (const Failure& error) {
            // An optional service may not implement an operation. Transport and trust failures are actionable.
            if (error.error != ErrorCode::camera_protocol_error)
                throw;
        }
    };
    if (control.imaging_source) {
        optional(control.imaging, imaging, "GetMoveOptions", source_parameter(control), [&](xmlNode* root) {
            auto* options = child(root, imaging, "MoveOptions");
            for (const auto* mode : {"Relative", "Continuous"}) {
                const bool relative = std::string_view(mode) == "Relative";
                auto* range = child(child(options, schema, mode), schema, relative ? "Distance" : "Speed");
                auto low = range_limit(range, "Min"), high = range_limit(range, "Max");
                if (!low || !high || *low > *high || *low > 0 || *high < 0 || *low == *high)
                    continue;
                for (bool near : {true, false}) {
                    const double speed = (near ? *low : *high) * (relative ? 0.05 : 0.3);
                    if (speed == 0)
                        continue;
                    auto& action = add(near ? "focus_near" : "focus_far");
                    action["mode"] = mode;
                    action["amount"] = speed;
                }
                add("focus_stop");
                break;
            }
        });
    }
    if (control.ptz_profile) {
        const auto profile = profile_parameter(control);
        optional(control.ptz, ptz, "GetPresets", profile, [&](xmlNode* root) {
            for (auto* n = root->children; n; n = n->next) {
                const auto token = attribute(n, "token");
                if (named(n, ptz, "Preset") && !token.empty())
                    add("preset_goto", token, child(n, schema, "Name") ? value(child(n, schema, "Name")) : token);
            }
        });
        optional(control.ptz, ptz, "GetPresetTours", profile, [&](xmlNode* root) {
            for (auto* n = root->children; n; n = n->next) {
                const auto token = attribute(n, "token");
                if (!named(n, ptz, "PresetTour") || token.empty())
                    continue;
                const auto title = child(n, schema, "Name") ? value(child(n, schema, "Name")) : token;
                auto* duration = child(child(n, schema, "StartingCondition"), schema, "RecurringDuration");
                const auto limit = duration ? seconds(value(duration)) : 0;
                const bool bounded =
                    limit > 0 && limit <= 60 && child(child(n, schema, "TourSpot"), schema, "PresetDetail");
                if (bounded)
                    add("tour_start", token, title)["duration"] = static_cast<int>(std::ceil(limit));
                auto* state = child(child(n, schema, "Status"), schema, "State");
                if (bounded || (state && (value(state) == "Touring" || value(state) == "Paused")))
                    add("tour_stop", token, title);
            }
        });
        if (control.node.has_value())
            optional(control.ptz, ptz, "GetNode", parameter("tptz:NodeToken", required_setting(control.node)),
                     [&](xmlNode* root) {
                         auto* node = child(root, ptz, "PTZNode");
                         if (attribute(node, "token") != required_setting(control.node))
                             return;
                         for (auto* n = node->children; n; n = n->next) {
                             if (!named(n, schema, "AuxiliaryCommands"))
                                 continue;
                             const auto token = value(n);
                             for (const auto& [name, command] : {std::pair{"tt:Wiper|On", "wiper_on"},
                                                                 {"tt:Wiper|Off", "wiper_off"},
                                                                 {"tt:IRLamp|On", "light_on"},
                                                                 {"tt:IRLamp|Off", "light_off"},
                                                                 {"LightOn", "light_on"},
                                                                 {"LightOff", "light_off"}})
                                 if (token == name)
                                     add(command, token);
                         }
                     });
    }
    optional(control.device_io, device, "GetRelayOutputs", {},
             [&](xmlNode* root) {
                 for (auto* n = root->children; n; n = n->next) {
                     const auto token = attribute(n, "token");
                     if (named(n, device, "RelayOutputs") && !token.empty()) {
                         add("relay_on", token);
                         add("relay_off", token);
                     }
                 }
             },
             {"http://www.onvif.org/ver10/deviceio/wsdl/GetRelayOutputs", {}});
    optional(control.device_io, device_io, "GetDigitalInputs", {},
             [&](xmlNode* root) {
                 for (auto* n = root->children; n && inputs.size() < 64; n = n->next)
                     if (const auto token = attribute(n, "token");
                         named(n, device_io, "DigitalInputs") && !token.empty())
                         inputs.push_back({{"token", token}});
             },
             {"http://www.onvif.org/ver10/deviceio/wsdl/GetDigitalInputs", {}});
    return {{"actions", actions}, {"inputs", inputs}};
}

void operate_camera(const CameraNetworks& networks, const CameraControl& control,
                    const CameraCredentials& credentials, const CameraAction& action)
{
    auto client = control_client(networks, control, credentials);
    client.actuator = true;
    const auto& cmd = action.command;
    const auto& target = action.target;
    if (cmd.starts_with("focus_")) {
        const auto source = source_parameter(control);
        auto stop = [&] {
            client.deadline = Clock::now() + 3s;
            client.call(required_setting(control.imaging), imaging, "Stop", source);
        };
        if (cmd == "focus_stop") {
            stop();
            return;
        }
        const auto& mode = action.mode;
        const bool pulse = mode == "Continuous";
        const auto movement = "<timg:Focus><tt:" + mode + ">" +
                              parameter(pulse ? "tt:Speed" : "tt:Distance", Json(action.amount).dump()) +
                              "</tt:" + mode + "></timg:Focus>";
        try {
            client.call(required_setting(control.imaging), imaging, "Move", source + movement);
        } catch (const Failure&) {
            if (pulse) {
                try {
                    stop();
                } catch (const Failure&) {
                    throw Failure{ErrorCode::camera_ptz_unknown};
                }
            }
            throw;
        }
        if (pulse) {
            std::this_thread::sleep_for(200ms);
            try {
                stop();
            } catch (const Failure&) {
                throw Failure{ErrorCode::camera_ptz_unknown};
            }
        }
        return;
    }
    if (cmd.starts_with("relay_")) {
        client.call(required_setting(control.device_io), device, "SetRelayOutputState",
                    parameter("tds:RelayOutputToken", target) +
                        parameter("tds:LogicalState", cmd == "relay_on" ? "active" : "inactive"),
                    {"http://www.onvif.org/ver10/deviceio/wsdl/SetRelayOutputState", {}});
        return;
    }
    auto profile = profile_parameter(control);
    if (cmd == "preset_goto")
        client.call(required_setting(control.ptz), ptz, "GotoPreset",
                    profile + parameter("tptz:PresetToken", target));
    else if (cmd == "tour_start" || cmd == "tour_stop")
        client.call(required_setting(control.ptz), ptz, "OperatePresetTour",
                    profile + parameter("tptz:PresetTourToken", target) +
                        parameter("tptz:Operation", cmd == "tour_start" ? "Start" : "Stop"));
    else
        client.call(required_setting(control.ptz), ptz, "SendAuxiliaryCommand",
                    profile + parameter("tptz:AuxiliaryData", target));
}

void move_onvif(const CameraNetworks& networks, const CameraControl& control, const CameraCredentials& credentials,
                const std::string& command)
{
    auto client = control_client(networks, control, credentials);
    client.actuator = true;
    auto profile = "<tptz:ProfileToken>" + escape(required_setting(control.profile)) + "</tptz:ProfileToken>";
    auto url = required_setting(control.ptz);
    if (command == "stop") {
        const auto& commands = control.directions;
        const auto supports = [&](const char* name) {
            return std::ranges::find(commands, name) != commands.end();
        };
        const bool pan = supports("left") || supports("right") || supports("up") || supports("down");
        const bool zoom = supports("zoom_in") || supports("zoom_out");
        client.call(url, ptz, "Stop",
                    profile + "<tptz:PanTilt>" + (pan ? "true" : "false") + "</tptz:PanTilt><tptz:Zoom>" +
                        (zoom ? "true" : "false") + "</tptz:Zoom>");
        return;
    }
    const bool relative = control.relative_move;
    const std::string amount = relative ? "0.03" : "0.5";
    std::string axes;
    if (command == "zoom_in" || command == "zoom_out") {
        axes = "<tt:Zoom space=\"" + std::string{relative ? relative_zoom : zoom_velocity_space} + "\" x=\"" +
               (command == "zoom_in" ? amount : "-" + amount) + "\"/>";
    } else {
        auto x = command == "left" ? "-" + amount : command == "right" ? amount : "0";
        auto y = command == "down" ? "-" + amount : command == "up" ? amount : "0";
        axes = "<tt:PanTilt space=\"" + std::string{relative ? relative_pan : velocity_space} + "\" x=\"" + x +
               "\" y=\"" + y + "\"/>";
    }
    if (relative)
        client.call(url, ptz, "RelativeMove", profile + "<tptz:Translation>" + axes + "</tptz:Translation>");
    else
        client.call(url, ptz, "ContinuousMove",
                    profile + "<tptz:Velocity>" + axes + "</tptz:Velocity><tptz:Timeout>PT1S</tptz:Timeout>");
}
} // namespace bacuri::vms::control
