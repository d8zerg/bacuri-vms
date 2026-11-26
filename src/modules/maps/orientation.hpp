#pragma once
#include <optional>
#include "application/control_api/api.hpp"
#include "modules/inventory/operations.hpp"
namespace bacuri::vms::control::maps_view
{
void calibrate(postgresql::Database&, const Request&, const std::string& site);
struct PanCalibration {
    std::string profile, space;
    double reference_pan{}, degrees_per_unit{};
    std::optional<double> tele_hfov;
};
struct Calibration {
    std::string device_id;
    double bearing{}, hfov{};
    std::optional<PanCalibration> pan;
};
enum class ViewState { unknown, fixed, live, stale };
struct ViewSector {
    ViewState state{ViewState::unknown};
    std::optional<double> bearing, hfov;
    std::string observed_at;
    bool estimated{};
};
ViewSector sector(const std::optional<Calibration>& calibration, const inventory::CameraOrientation& camera);
} // namespace bacuri::vms::control::maps_view
