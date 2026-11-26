#pragma once
#include "application/control_api/http_json.hpp"
#include "modules/maps/orientation.hpp"
namespace bacuri::vms::control::maps_view
{
inline void from_json(const Json& j, Calibration& c)
{
    c = {j.at("device_id"), j.at("bearing"), j.at("hfov"), {}};
    if (j.at("ptz").get<bool>()) {
        auto& pan = c.pan.emplace(
            PanCalibration{j.at("profile"), j.at("space"), j.at("reference_pan"), j.at("degrees_per_unit"), {}});
        if (j.contains("tele_hfov"))
            pan.tele_hfov = j.at("tele_hfov").get<double>();
    }
}
inline void to_json(Json& j, const Calibration& c)
{
    j = {{"device_id", c.device_id}, {"bearing", c.bearing}, {"hfov", c.hfov}, {"ptz", c.pan.has_value()}};
    if (c.pan) {
        const auto& p = *c.pan;
        j.update(Json{{"profile", p.profile},
                      {"space", p.space},
                      {"reference_pan", p.reference_pan},
                      {"degrees_per_unit", p.degrees_per_unit}});
        if (p.tele_hfov)
            j["tele_hfov"] = *p.tele_hfov;
    }
}
inline void to_json(Json& j, const ViewSector& v)
{
    switch (v.state) {
    case ViewState::unknown:
        j = {{"state", "unknown"}};
        break;
    case ViewState::fixed:
        j = {{"state", "fixed"}};
        break;
    case ViewState::live:
        j = {{"state", "live"}, {"observed_at", v.observed_at}};
        break;
    case ViewState::stale:
        j = {{"state", "stale"}, {"observed_at", v.observed_at}};
        break;
    }
    if (v.bearing)
        j["bearing"] = *v.bearing;
    if (v.hfov)
        j["hfov"] = *v.hfov;
    if (v.estimated)
        j["estimated"] = true;
}
} // namespace bacuri::vms::control::maps_view
