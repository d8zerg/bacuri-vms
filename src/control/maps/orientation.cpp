#include "control/maps/orientation.hpp"
#include "control/inventory/operations.hpp"
#include "control/inventory/telemetry.hpp"
#include <cmath>
namespace bacuri::vms::control::maps_view
{
namespace
{
double number(const Json& body, const char* key, double min, double max)
{
    auto it = body.find(key);
    if (it == body.end() || !it->is_number() || !std::isfinite(it->get<double>()) || *it < min || *it > max)
        throw Failure{400, "invalid_request"};
    return it->get<double>();
}
} // namespace
void calibrate(Database& db, const Request& req, const std::string& site)
{
    auto b = object_body(req, {"point_id", "plan_id", "device_id", "bearing", "hfov", "tele_hfov", "reference_pan",
                               "space", "degrees_per_unit"});
    auto point = string_field(b, "point_id", 36), device = string_field(b, "device_id", 36);
    if (!canonical_id(point) || !canonical_id(device) || !b.contains("plan_id"))
        throw Failure{400, "invalid_request"};
    std::string plan;
    if (!b.at("plan_id").is_null()) {
        plan = string_field(b, "plan_id", 36);
        if (!canonical_id(plan))
            throw Failure{400, "invalid_request"};
    }
    if (!inventory::lock_point(db, site, point))
        throw Failure{404, "not_found"};
    if (plan.empty() &&
        db.exec("SELECT FROM bacuri_maps.point_placements WHERE site_id=$1 AND point_id=$2 AND geo_primary",
                {site, point})
            .size())
        throw Failure{409, "position_from_plan"};
    auto cameras = inventory::camera_orientations(db, site, {point});
    if (!cameras.contains(point) || cameras.at(point).at("device_id") != device)
        throw Failure{409, "camera_binding_conflict"};
    const auto& cam = cameras.at(point);
    if (!inventory::lock_camera_revision(db, site, point, device, cam.at("revision")))
        throw Failure{409, "camera_binding_conflict"};
    Json config{{"device_id", device},
                {"bearing", number(b, "bearing", 0, 360)},
                {"hfov", number(b, "hfov", 0.1, 179.9)},
                {"ptz", cam.at("ptz")}};
    if (cam.at("ptz") == true) {
        if (!cam.value("fresh", false) || cam.value("moving", false) || !b.contains("space") ||
            b.at("space") != cam.at("space") ||
            std::abs(number(b, "reference_pan", -1e9, 1e9) - cam.at("pan").get<double>()) > 0.00001)
            throw Failure{409, "camera_position_changed"};
        config["profile"] = cam.at("profile");
        config["space"] = cam.at("space");
        config["reference_pan"] = cam.at("pan");
        const double scale =
            cam.at("space") == "http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionSpaceDegrees"
                ? 1.0
                : number(b, "degrees_per_unit", -36000, 36000);
        if (scale == 0)
            throw Failure{400, "invalid_request"};
        config["degrees_per_unit"] = scale;
        if (b.contains("tele_hfov"))
            config["tele_hfov"] = number(b, "tele_hfov", 0.1, config.at("hfov"));
    }
    if (!db.exec("UPDATE bacuri_maps.point_placements SET calibration=$4 WHERE site_id=$1 AND point_id=$2 AND "
                 "plan_id IS NOT DISTINCT FROM nullif($3,'')::uuid RETURNING point_id",
                 {site, point, plan, config.dump()})
             .size())
        throw Failure{404, "not_found"};
}
Json sector(const Json& cfg, const Json& cam)
{
    Json unknown{{"state", "unknown"}};
    if (!cfg.is_object() || !cam.is_object() || cfg.at("device_id") != cam.at("device_id") ||
        cfg.at("ptz") != cam.at("ptz"))
        return unknown;
    double bearing = cfg.at("bearing"), width = cfg.at("hfov");
    if (cfg.at("ptz") == false)
        return {{"state", "fixed"}, {"bearing", bearing}, {"hfov", width}};
    if (!cam.contains("pan") || cam.at("space") != cfg.at("space") || cam.at("profile") != cfg.at("profile"))
        return unknown;
    bearing += (cam.at("pan").get<double>() - cfg.at("reference_pan").get<double>()) *
               cfg.at("degrees_per_unit").get<double>();
    auto angle = std::fmod(bearing, 360.0);
    Json result{{"bearing", angle < 0 ? angle + 360 : angle},
                {"state", cam.at("fresh") == true ? "live" : "stale"},
                {"observed_at", cam.at("observed_at")}};
    if (cam.contains("hfov"))
        result["hfov"] = cam.at("hfov");
    else if (cam.contains("zoom") && cfg.contains("tele_hfov")) {
        result["hfov"] = std::lerp(width, cfg.at("tele_hfov").get<double>(), cam.at("zoom").get<double>());
        result["estimated"] = true;
    } else {
        result["hfov"] = width;
        result["estimated"] = true;
    }
    return result;
}
} // namespace bacuri::vms::control::maps_view
