#pragma once
#include <optional>
#include "application/control_api/api.hpp"
#include <variant>

namespace bacuri::vms::control::maps_geo
{
struct Coordinates {
    double lng, lat;
};
struct PlanPosition {
    double x, y;
};
struct SurveyAnchor {
    double x{}, y{}, lng{}, lat{};
};
struct SurveyScale {
    double width_m{}, bearing{};
};
struct PlanDefinition {
    SurveyAnchor anchor{};
    std::variant<SurveyAnchor, SurveyScale> calibration;
    std::optional<std::string> building;
};
struct Footprint {
    std::vector<Coordinates> corners;
    double bearing{};
    Coordinates center{};
    double width_m{};
};
struct ProjectedCamera {
    std::string point_id;
    Coordinates projected{};
    std::optional<std::string> existing_plan_id;
    std::optional<Coordinates> existing;
    std::optional<double> distance_m;
};
struct GeoreferencePreview {
    std::vector<ProjectedCamera> cameras;
    std::optional<Footprint> footprint;
    std::string token;
};
class PlanTransform
{
  public:
    PlanTransform(const PlanDefinition& definition, int width, int height);
    Coordinates project(PlanPosition position) const;
    double heading() const;
    Footprint footprint() const;

  private:
    int width_, height_;
    double x_, y_, right_east_, up_east_, longitude_, latitude_, mercator_y_;
};
void lock_geography(postgresql::Database&, const std::string& site);
GeoreferencePreview georeference(postgresql::QuerySession&, const Request&, const std::string& site, bool save);
} // namespace bacuri::vms::control::maps_geo
