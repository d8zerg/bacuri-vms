#pragma once
#include "modules/inventory/operations.hpp"
#include <nlohmann/json.hpp>

namespace bacuri::vms::control::inventory
{
inline void from_json(const nlohmann::json& j, CameraPosition& p)
{
    p = {j.at("pan"), j.at("space"), {}, {}, {}, {}, {}};
    if (j.contains("tilt"))
        p.tilt = j.at("tilt").get<double>();
    if (j.contains("zoom"))
        p.zoom = j.at("zoom").get<double>();
    if (j.contains("hfov"))
        p.hfov = j.at("hfov").get<double>();
    if (j.contains("moving"))
        p.moving = j.at("moving").get<bool>();
    if (j.contains("source_time"))
        p.source_time = j.at("source_time").get<std::string>();
}
inline void to_json(nlohmann::json& j, const CameraPosition& p)
{
    j = {{"pan", p.pan}, {"space", p.space}};
    if (p.tilt)
        j["tilt"] = *p.tilt;
    if (p.zoom)
        j["zoom"] = *p.zoom;
    if (p.hfov)
        j["hfov"] = *p.hfov;
    if (p.moving)
        j["moving"] = *p.moving;
    if (p.source_time)
        j["source_time"] = *p.source_time;
}
inline void to_json(nlohmann::json& j, const CameraOrientation& camera)
{
    j = {{"device_id", camera.device_id},
         {"ptz", camera.ptz},
         {"profile", camera.profile},
         {"revision", camera.revision}};
    if (camera.position) {
        j.update(nlohmann::json(*camera.position));
        j["observed_at"] = camera.observed_at;
        j["fresh"] = camera.fresh;
    }
}
} // namespace bacuri::vms::control::inventory
