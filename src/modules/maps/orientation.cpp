#include "application/control_api/http_json.hpp"
#include "modules/maps/orientation_json.hpp"
#include "modules/maps/orientation.hpp"
#include "modules/inventory/operations.hpp"
#include <cmath>
namespace bacuri::vms::control::maps_view
{
namespace
{
double number(const Json& body, const char* key, double min, double max)
{
    auto it = body.find(key);
    if (it == body.end() || !it->is_number() || !std::isfinite(it->get<double>()) || *it < min || *it > max)
        throw Failure{ErrorCode::invalid_request};
    return it->get<double>();
}
} // namespace
void calibrate(postgresql::Database& db, const Request& req, const std::string& site)
{
    auto b = object_body(req, {"point_id", "plan_id", "device_id", "bearing", "hfov", "tele_hfov", "reference_pan",
                               "space", "degrees_per_unit"});
    auto point = string_field(b, "point_id", 36), device = string_field(b, "device_id", 36);
    if (!canonical_id(point) || !canonical_id(device) || !b.contains("plan_id"))
        throw Failure{ErrorCode::invalid_request};
    std::string plan;
    if (!b.at("plan_id").is_null()) {
        plan = string_field(b, "plan_id", 36);
        if (!canonical_id(plan))
            throw Failure{ErrorCode::invalid_request};
    }
    if (!inventory::lock_point(db, site, point))
        throw Failure{ErrorCode::not_found};
    if (plan.empty() &&
        db.exec("SELECT FROM bacuri_maps.point_placements WHERE site_id=$1 AND point_id=$2 AND geo_primary",
                {site, point})
            .size())
        throw Failure{ErrorCode::position_from_plan};
    auto cameras = inventory::camera_orientations(db, site, {point});
    if (!cameras.contains(point) || cameras.at(point).device_id != device)
        throw Failure{ErrorCode::camera_binding_conflict};
    const auto& cam = cameras.at(point);
    if (!inventory::lock_camera_revision(db, site, point, device, cam.revision))
        throw Failure{ErrorCode::camera_binding_conflict};
    Calibration config{device, number(b, "bearing", 0, 360), number(b, "hfov", 0.1, 179.9), {}};
    if (cam.ptz) {
        if (!cam.position || !cam.fresh || cam.position->moving.value_or(false) || !b.contains("space") ||
            b.at("space") != cam.position->space ||
            std::abs(number(b, "reference_pan", -1e9, 1e9) - cam.position->pan) > 0.00001)
            throw Failure{ErrorCode::camera_position_changed};
        const double scale =
            cam.position->space == "http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionSpaceDegrees"
                ? 1.0
                : number(b, "degrees_per_unit", -36000, 36000);
        if (scale == 0)
            throw Failure{ErrorCode::invalid_request};
        auto& pan =
            config.pan.emplace(PanCalibration{cam.profile, cam.position->space, cam.position->pan, scale, {}});
        if (b.contains("tele_hfov"))
            pan.tele_hfov = number(b, "tele_hfov", 0.1, config.hfov);
    }
    if (!db.exec("UPDATE bacuri_maps.point_placements SET calibration=$4 WHERE site_id=$1 AND point_id=$2 AND "
                 "plan_id IS NOT DISTINCT FROM nullif($3,'')::uuid RETURNING point_id",
                 {site, point, plan, Json(config).dump()})
             .size())
        throw Failure{ErrorCode::not_found};
}
ViewSector sector(const std::optional<Calibration>& calibration, const inventory::CameraOrientation& cam)
{
    if (!calibration || calibration->device_id != cam.device_id || calibration->pan.has_value() != cam.ptz)
        return {};
    const auto& cfg = *calibration;
    if (!cfg.pan)
        return {ViewState::fixed, cfg.bearing, cfg.hfov, {}, false};
    const auto& pan = *cfg.pan;
    if (!cam.position || cam.position->space != pan.space || cam.profile != pan.profile)
        return {};
    const auto angle =
        std::fmod(cfg.bearing + (cam.position->pan - pan.reference_pan) * pan.degrees_per_unit, 360.);
    ViewSector result{cam.fresh ? ViewState::live : ViewState::stale, angle < 0 ? angle + 360 : angle, cfg.hfov,
                      cam.observed_at, false};
    if (cam.position->hfov)
        result.hfov = cam.position->hfov;
    else {
        result.estimated = true;
        if (cam.position->zoom && pan.tele_hfov)
            result.hfov = std::lerp(cfg.hfov, *pan.tele_hfov, *cam.position->zoom);
    }
    return result;
}
} // namespace bacuri::vms::control::maps_view
