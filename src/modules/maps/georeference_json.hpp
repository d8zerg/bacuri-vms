#pragma once
#include "application/control_api/http_json.hpp"
#include "modules/maps/georeference.hpp"
namespace bacuri::vms::control::maps_geo
{
inline void from_json(const Json& j, PlanDefinition& d)
{
    auto require = [](bool b) {
        if (!b)
            throw Failure{ErrorCode::invalid_georeference};
    };
    auto number = [&](const Json& item, const char* key) {
        auto it = item.find(key);
        require(it != item.end() && it->is_number());
        return it->get<double>();
    };
    auto anchor = [&](const Json& a) {
        require(a.is_object() && a.size() == 4);
        return SurveyAnchor{number(a, "x"), number(a, "y"), number(a, "lng"), number(a, "lat")};
    };
    require(j.is_object() && j.contains("anchor"));
    d = {anchor(j.at("anchor")), SurveyScale{}, {}};
    if (j.contains("building"))
        d.building = string_field(j, "building", 128);
    if (j.contains("second")) {
        require(j.size() == 2u + d.building.has_value());
        d.calibration = anchor(j.at("second"));
    } else {
        require(j.size() == 3u + d.building.has_value());
        d.calibration = SurveyScale{number(j, "width_m"), number(j, "bearing")};
    }
}
inline void to_json(Json& j, const SurveyAnchor& a)
{
    j = {{"x", a.x}, {"y", a.y}, {"lng", a.lng}, {"lat", a.lat}};
}
inline void to_json(Json& j, const PlanDefinition& d)
{
    j = {{"anchor", d.anchor}};
    if (d.building)
        j["building"] = *d.building;
    if (const auto* a = std::get_if<SurveyAnchor>(&d.calibration))
        j["second"] = *a;
    else {
        const auto& c = std::get<SurveyScale>(d.calibration);
        j["width_m"] = c.width_m;
        j["bearing"] = c.bearing;
    }
}
inline void to_json(Json& j, const Coordinates& c)
{
    j = Json::array({c.lng, c.lat});
}
inline void to_json(Json& j, const Footprint& f)
{
    j = {{"corners", f.corners}, {"bearing", f.bearing}, {"center", f.center}, {"width_m", f.width_m}};
}
inline void to_json(Json& j, const ProjectedCamera& c)
{
    j = {{"point_id", c.point_id}, {"projected", c.projected}};
    if (c.existing)
        j["existing"] = *c.existing;
    if (c.existing_plan_id)
        j["existing_plan_id"] = *c.existing_plan_id;
    if (c.distance_m)
        j["distance_m"] = *c.distance_m;
}
inline void to_json(Json& j, const GeoreferencePreview& p)
{
    j = {{"cameras", p.cameras}, {"token", p.token}};
    if (p.footprint)
        j["footprint"] = *p.footprint;
}
} // namespace bacuri::vms::control::maps_geo
