#pragma once
#include "application/control_api/api.hpp"

namespace bacuri::vms::control::maps_geo
{
struct Coordinates {
    double lng, lat;
};
struct PlanPosition {
    double x, y;
};
class PlanTransform
{
  public:
    PlanTransform(const Json& definition, int width, int height);
    Coordinates project(PlanPosition position) const;
    double heading() const;
    Json footprint() const;

  private:
    int width_, height_;
    double x_, y_, right_east_, up_east_, longitude_, latitude_, mercator_y_;
};
void lock_geography(postgresql::Database&, const std::string& site);
Json georeference(postgresql::Database&, const Request&, const std::string& site, bool save);
} // namespace bacuri::vms::control::maps_geo
