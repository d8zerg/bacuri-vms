#pragma once
#include "application/control_api/http_json.hpp"
#include "modules/inventory/device.hpp"
#include "application/control_api/playback_time_json.hpp"
namespace bacuri::vms::control
{
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(BrightnessRange, min, max, value)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DiscoveredCamera, endpoint_id, device_service, selection)
inline void to_json(Json& j, const CameraProfile& p)
{
    j = {{"token", p.token}, {"name", p.name}, {"url", p.url}, {"channel", p.channel}, {"codec", p.codec}};
    if (p.width)
        j["width"] = *p.width;
    if (p.height)
        j["height"] = *p.height;
}
inline void to_json(Json& j, const InspectedCamera& c)
{
    j = {{"endpoint_id", c.endpoint_id},
         {"manufacturer", c.manufacturer},
         {"model", c.model},
         {"firmware", c.firmware},
         {"serial", c.serial},
         {"selection", c.selection},
         {"ptz", c.ptz},
         {"events", c.events},
         {"profiles", c.profiles}};
}
inline void to_json(Json& j, const DeviceControls& c)
{
    j = {{"actions", Json::array()}, {"inputs", Json::array()}};
    for (const auto& a : c.actions)
        j["actions"].push_back({{"command", a.command}, {"target", a.target}, {"label", a.label}});
    for (const auto& token : c.inputs)
        j["inputs"].push_back({{"token", token}});
}
inline void to_json(Json& j, const EdgeArchive& a)
{
    j = {{"supported", a.supported}, {"recordings", Json::array()}};
    for (const auto& r : a.recordings) {
        Json tracks = Json::array();
        for (const auto& t : r.tracks)
            tracks.push_back({{"kind", t.kind},
                              {"bounds", t.bounds ? Window{t.bounds->first, t.bounds->second} : Json(nullptr)}});
        j["recordings"].push_back({{"id", r.id}, {"name", r.name}, {"tracks", std::move(tracks)}});
    }
}
} // namespace bacuri::vms::control
