#include "modules/maps/georeference.hpp"
#include <cmath>
#include <numbers>
#include <set>

namespace bacuri::vms::control::maps_geo
{
namespace
{
constexpr double radians = std::numbers::pi / 180, radius = 6378137, latitude_limit = 85.051129;
void require(bool condition)
{
    if (!condition)
        throw Failure{ErrorCode::invalid_georeference};
}
double number(const Json& j, const char* key, double low, double high)
{
    auto it = j.find(key);
    require(it != j.end() && it->is_number());
    double value = it->get<double>();
    require(value >= low && value <= high);
    return value;
}
void anchor(const Json& a)
{
    require(a.is_object() && a.size() == 4);
    number(a, "x", 0, 1);
    number(a, "y", 0, 1);
    number(a, "lng", -180, 180);
    number(a, "lat", -latitude_limit, latitude_limit);
}
double mercator(double lat)
{
    return std::asinh(std::tan(lat * radians));
}
double distance(Coordinates a, Coordinates b)
{
    const double dy = std::sin((a.lat - b.lat) * radians / 2), dx = std::sin((a.lng - b.lng) * radians / 2);
    return 2 * radius *
           std::asin(std::sqrt(
               std::clamp(dy * dy + dx * dx * std::cos(a.lat * radians) * std::cos(b.lat * radians), 0.0, 1.0)));
}
} // namespace
PlanTransform::PlanTransform(const Json& d, int width, int height) : width_(width), height_(height)
{
    require(d.is_object() && d.contains("anchor") && width > 0 && height > 0);
    if (d.contains("building"))
        string_field(d, "building", 128);
    const auto& a = d.at("anchor");
    anchor(a);
    x_ = a.at("x");
    y_ = a.at("y");
    longitude_ = a.at("lng");
    latitude_ = a.at("lat");
    mercator_y_ = mercator(latitude_);
    const auto ground = std::cos(latitude_ * radians);
    if (d.contains("second")) {
        require(d.size() == 2u + d.contains("building"));
        const auto& b = d.at("second");
        anchor(b);
        const double u = (b.at("x").get<double>() - x_) * width, v = (y_ - b.at("y").get<double>()) * height;
        const double e = radius * radians * std::remainder(b.at("lng").get<double>() - longitude_, 360.0),
                     n = radius * (mercator(b.at("lat")) - mercator_y_);
        require(std::hypot(u, v) >= 1 && std::hypot(e, n) * ground >= 0.25);
        right_east_ = (e * u + n * v) / (u * u + v * v);
        up_east_ = (e * v - n * u) / (u * u + v * v);
    } else {
        require(d.size() == 3u + d.contains("building"));
        const double scale = number(d, "width_m", 0.25, 40075016.7) / (width * ground);
        const double angle = number(d, "bearing", 0, 360) * radians;
        right_east_ = scale * std::cos(angle);
        up_east_ = scale * std::sin(angle);
    }
    // A single local image must fit on one longitude branch and within the Mercator latitude range.
    for (double x : {0., 1.})
        for (double y : {0., 1.})
            (void)project({x, y});
}
Coordinates PlanTransform::project(PlanPosition position) const
{
    const double u = (position.x - x_) * width_, v = (y_ - position.y) * height_;
    const double delta = (right_east_ * u + up_east_ * v) / radius / radians;
    const double lat = std::atan(std::sinh(mercator_y_ + (-up_east_ * u + right_east_ * v) / radius)) / radians;
    require(std::abs(delta) < 180 && std::abs(lat) <= latitude_limit);
    return {std::remainder(longitude_ + delta, 360.0), lat};
}
double PlanTransform::heading() const
{
    return std::fmod(std::atan2(up_east_, right_east_) / radians + 360, 360);
}
Json PlanTransform::footprint() const
{
    Json corners = Json::array();
    for (auto [x, y] : {std::pair{0., 0.}, {1., 0.}, {1., 1.}, {0., 1.}}) {
        auto [lng, lat] = project({x, y});
        corners.push_back({longitude_ + std::remainder(lng - longitude_, 360.0), lat});
    }
    const auto center = project({.5, .5});
    return {{"corners", corners},
            {"bearing", heading()},
            {"center", {center.lng, center.lat}},
            {"width_m", std::hypot(right_east_, up_east_) * width_ * std::cos(latitude_ * radians)}};
}
void lock_geography(postgresql::Database& db, const std::string& site)
{
    db.exec("SELECT pg_advisory_xact_lock(hashtextextended('bacuri_maps.geography:' || $1,0))", {site});
}
Json georeference(postgresql::Database& db, const Request& req, const std::string& site, bool save)
{
    auto body = object_body(req, {"plan_id", "definition", "token", "choices"});
    const auto id = string_field(body, "plan_id", 36);
    require(canonical_id(id) && body.contains("definition"));
    auto plan =
        db.exec("SELECT width,height,georeference FROM bacuri_maps.floor_plans WHERE site_id=$1 AND plan_id=$2",
                {site, id});
    if (!plan.size())
        throw Failure{ErrorCode::not_found};
    const auto& definition = body.at("definition");
    std::optional<PlanTransform> transform;
    Json preview{{"cameras", Json::array()}}, choices = body.value("choices", Json::object());
    require(choices.is_object());
    if (!definition.is_null()) {
        transform.emplace(definition, std::stoi(plan.get(0)), std::stoi(plan.get(0, 1)));
        preview["footprint"] = transform->footprint();
    }
    // Preserve the manual placement even when a plan supplies the geographic position.
    auto rows = db.exec(R"sql(
        SELECT l.point_id,l.x,l.y,l.geo_primary,s.plan_id,s.x,s.y,p.georeference,p.width,p.height
        FROM bacuri_maps.point_placements l
        LEFT JOIN LATERAL (
            SELECT m.plan_id,m.x,m.y FROM bacuri_maps.point_placements m
            WHERE m.site_id=l.site_id AND m.point_id=l.point_id AND
                (m.plan_id IS NULL OR (m.geo_primary AND m.plan_id<>l.plan_id))
            ORDER BY m.geo_primary DESC NULLS LAST LIMIT 1
        ) s ON true
        LEFT JOIN bacuri_maps.floor_plans p ON p.site_id=l.site_id AND p.plan_id=s.plan_id
        WHERE l.site_id=$1 AND l.plan_id=$2 ORDER BY l.point_id
    )sql",
                        {site, id});
    Json state = {site, id, plan.get(0, 2), definition}, selected = Json::array();
    std::set<std::string> conflicts;
    for (int i = 0; i < rows.size(); ++i) {
        const auto point = rows.get(i);
        state.push_back({point, rows.get(i, 1), rows.get(i, 2), rows.get(i, 3), rows.get(i, 4), rows.get(i, 5),
                         rows.get(i, 6), rows.get(i, 7)});
        if (!transform)
            continue;
        auto projected = transform->project({std::stod(rows.get(i, 1)), std::stod(rows.get(i, 2))});
        Json camera{{"point_id", point}, {"projected", {projected.lng, projected.lat}}};
        if (!rows.get(i, 5).empty()) {
            Coordinates existing{std::stod(rows.get(i, 5)), std::stod(rows.get(i, 6))};
            if (!rows.get(i, 4).empty()) {
                PlanTransform other(Json::parse(rows.get(i, 7)), std::stoi(rows.get(i, 8)),
                                    std::stoi(rows.get(i, 9)));
                existing = other.project({existing.lng, existing.lat});
                camera["existing_plan_id"] = rows.get(i, 4);
            }
            camera["existing"] = {existing.lng, existing.lat};
            camera["distance_m"] = distance(projected, existing);
            conflicts.insert(point);
        }
        if (!camera.contains("existing") || (choices.contains(point) && choices.at(point) == "plan"))
            selected.push_back(point);
        preview["cameras"].push_back(std::move(camera));
    }
    preview["token"] = db.exec("SELECT encode(sha256(convert_to($1,'UTF8')),'hex')", {state.dump()}).get(0);
    if (!save)
        return preview;
    if (body.value("token", Json()) != preview.at("token"))
        throw Failure{ErrorCode::georeference_changed};
    require(choices.size() == conflicts.size());
    for (const auto& point : conflicts)
        require(choices.contains(point) && (choices.at(point) == "keep" || choices.at(point) == "plan"));
    db.exec(
        "UPDATE bacuri_maps.floor_plans SET georeference=nullif($3,'null')::jsonb WHERE site_id=$1 AND plan_id=$2",
        {site, id, definition.dump()});
    const auto ids = postgresql::array_parameter(selected.get<std::vector<std::string>>());
    db.exec("UPDATE bacuri_maps.point_placements SET geo_primary=NULL WHERE site_id=$1 AND geo_primary AND "
            "(plan_id=$2 OR point_id=ANY($3::uuid[]))",
            {site, id, ids});
    db.exec("UPDATE bacuri_maps.point_placements SET geo_primary=true WHERE site_id=$1 AND plan_id=$2 AND "
            "point_id=ANY($3::uuid[])",
            {site, id, ids});
    return preview;
}
} // namespace bacuri::vms::control::maps_geo
