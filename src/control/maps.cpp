#include "control/inventory/operations.hpp"
#include "control/access/operations.hpp"
#include "control/maps.hpp"
#include "control/maps/packages.hpp"
#include <sodium.h>
#include <charconv>

namespace bacuri::vms::control
{
namespace
{
void valid(bool condition)
{
    if (!condition)
        throw Failure{400, "invalid_request"};
}
void found(bool condition)
{
    if (!condition)
        throw Failure{404, "not_found"};
}
Json scene(Database& db, const std::string& site, const std::string& user, bool admin)
{
    Json result{{"region", nullptr},
                {"regions", Json::array()},
                {"plans", Json::array()},
                {"placements", Json::array()},
                {"symbols", "/assets/sprite.svg"},
                {"fonts", {"/assets/inter-latin.woff2", "/assets/inter-cyrillic.woff2"}}};
    const auto base = "/api/v1/sites/" + site + "/maps/";
    auto region = db.exec("SELECT revision,name,to_char(source_time AT TIME ZONE "
                          "'UTC','YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'),bounds,min_zoom,max_zoom,region_id FROM "
                          "bacuri_maps.map_regions WHERE site_id=$1 ORDER BY name,region_id",
                          {site});
    for (int i = 0; i < region.size(); ++i)
        result["regions"].push_back({{"id", region.get(i, 6)},
                                     {"revision", region.get(i)},
                                     {"name", region.get(i, 1)},
                                     {"source_time", region.get(i, 2)},
                                     {"bounds", Json::parse(region.get(i, 3))},
                                     {"min_zoom", std::stoi(region.get(i, 4))},
                                     {"max_zoom", std::stoi(region.get(i, 5))},
                                     {"style_url", base + region.get(i) + "/style"},
                                     {"attribution", "© OpenStreetMap contributors · ODbL"}});
    if (region.size())
        result["region"] = result["regions"].front();
    const auto points = admin ? "{}" : array_parameter(access::granted_point_ids(db, site, user));
    auto plans =
        db.exec("SELECT plan_id,name,width,height FROM bacuri_maps.floor_plans p WHERE site_id=$1 AND "
                "($3::boolean OR EXISTS(SELECT FROM bacuri_maps.point_placements l WHERE l.site_id=p.site_id AND "
                "l.plan_id=p.plan_id AND l.point_id=ANY($2::uuid[]))) ORDER BY name,plan_id",
                {site, points, admin ? "true" : "false"});
    for (int i = 0; i < plans.size(); ++i)
        result["plans"].push_back({{"id", plans.get(i)},
                                   {"name", plans.get(i, 1)},
                                   {"width", std::stoi(plans.get(i, 2))},
                                   {"height", std::stoi(plans.get(i, 3))},
                                   {"image_url", base + "plans/" + plans.get(i)}});
    auto markers =
        db.exec("SELECT point_id,plan_id,x,y FROM bacuri_maps.point_placements "
                "WHERE site_id=$1 AND ($3::boolean OR point_id=ANY($2::uuid[])) ORDER BY point_id,plan_id",
                {site, points, admin ? "true" : "false"});
    for (int i = 0; i < markers.size(); ++i)
        result["placements"].push_back(
            {{"point_id", markers.get(i)},
             {"plan_id", markers.get(i, 1).empty() ? Json(nullptr) : Json(markers.get(i, 1))},
             {"x", std::stod(markers.get(i, 2))},
             {"y", std::stod(markers.get(i, 3))}});
    return result;
}
void png(Response& res, const Rows& rows)
{
    found(rows.size());
    gsize length{};
    std::unique_ptr<guchar, decltype(&g_free)> bytes{g_base64_decode(rows.get(0).c_str(), &length), g_free};
    res.result(http::status::ok);
    res.set(http::field::content_type, "image/png");
    res.body().assign(reinterpret_cast<const char*>(bytes.get()), length);
}
int tile_coordinate(std::string_view value, int max)
{
    int n{};
    const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), n);
    found(ec == std::errc{} && end == value.data() + value.size() && n >= 0 && n <= max);
    return n;
}
void region_resource(Database& db, Response& res, const std::string& site, std::string_view path,
                     const std::string& origin)
{
    found(path.size() > 37 && canonical_id(path.substr(0, 36)) && path[36] == '/');
    const std::string revision{path.substr(0, 36)};
    path.remove_prefix(37);
    if (path == "style") {
        auto rows = db.exec(
            "SELECT bounds,min_zoom,max_zoom FROM bacuri_maps.map_regions WHERE site_id=$1 AND revision=$2",
            {site, revision});
        found(rows.size());
        const auto url = origin + "/api/v1/sites/" + site + "/maps/" + revision + "/tiles/{z}/{x}/{y}.png";
        const Json source{{"type", "raster"},
                          {"tiles", {url}},
                          {"tileSize", 256},
                          {"bounds", Json::parse(rows.get(0))},
                          {"minzoom", std::stoi(rows.get(0, 1))},
                          {"maxzoom", std::stoi(rows.get(0, 2))},
                          {"attribution", "© OpenStreetMap contributors · ODbL"}};
        respond(res, {{"version", 8},
                      {"sources", {{"region", source}}},
                      {"layers", Json::array({{{"id", "region"}, {"type", "raster"}, {"source", "region"}}})}});
        return;
    }
    found(path.starts_with("tiles/") && path.ends_with(".png"));
    path.remove_prefix(6);
    path.remove_suffix(4);
    auto split = path.find('/');
    found(split != path.npos);
    const int z = tile_coordinate(path.substr(0, split), 22), max = (1 << z) - 1;
    path.remove_prefix(split + 1);
    split = path.find('/');
    found(split != path.npos);
    const int x = tile_coordinate(path.substr(0, split), max), y = tile_coordinate(path.substr(split + 1), max);
    png(res, db.exec("SELECT encode(image,'base64') FROM bacuri_maps.map_tiles JOIN bacuri_maps.map_regions "
                     "USING(site_id,region_id) WHERE "
                     "site_id=$1 AND revision=$2 AND z=$3 AND x=$4 AND y=$5",
                     {site, revision, std::to_string(z), std::to_string(x), std::to_string(y)}));
}
Json add_plan(Database& db, const Request& req, const std::string& site)
{
    const auto body = object_body(req, {"name", "png"});
    const auto name = string_field(body, "name", 128);
    valid(body.contains("png") && body["png"].is_string());
    const auto& encoded = body["png"].get_ref<const std::string&>();
    constexpr std::size_t maximum = 4uz * 1024 * 1024;
    valid(encoded.size() <= 4 * ((maximum + 2) / 3));
    std::string bytes(encoded.size() / 4 * 3 + 3, '\0');
    std::size_t length{};
    valid(sodium_base642bin(reinterpret_cast<unsigned char*>(bytes.data()), bytes.size(), encoded.data(),
                            encoded.size(), nullptr, &length, nullptr, sodium_base64_VARIANT_ORIGINAL) == 0);
    bytes.resize(length);
    MapImage image;
    try {
        image = read_map_png(bytes, false);
    } catch (const std::invalid_argument&) {
        throw Failure{400, "invalid_request"};
    }
    found(inventory::lock_site(db, site));
    valid(db.exec("SELECT coalesce(sum(octet_length(image)),0)+$2::integer<=16777216 FROM bacuri_maps.floor_plans "
                  "WHERE "
                  "site_id=$1",
                  {site, std::to_string(image.png.size())})
              .get(0) == "t");
    std::unique_ptr<char, decltype(&g_free)> clean{
        g_base64_encode(reinterpret_cast<const guchar*>(image.png.data()), image.png.size()), g_free};
    auto plan = db.exec("INSERT INTO bacuri_maps.floor_plans(site_id,name,image,width,height) VALUES "
                        "($1,$2,decode($3,'base64'),$4,$5) RETURNING plan_id",
                        {site, name, clean.get(), std::to_string(image.width), std::to_string(image.height)});
    return {{"id", plan.get(0)}};
}
void place(Database& db, const Request& req, const std::string& site)
{
    auto body = object_body(req, {"point_id", "plan_id", "x", "y"});
    auto point = string_field(body, "point_id", 36);
    valid(canonical_id(point) && body.contains("plan_id") && body.value("x", Json()).is_number() &&
          body.value("y", Json()).is_number());
    std::string plan;
    if (!body["plan_id"].is_null()) {
        plan = string_field(body, "plan_id", 36);
        valid(canonical_id(plan));
    }
    const double x = body["x"], y = body["y"];
    valid(plan.empty() ? (std::abs(x) <= 180 && std::abs(y) <= 85.051129)
                       : (x >= 0 && x <= 1 && y >= 0 && y <= 1));
    found(inventory::lock_point(db, site, point));
    if (plan.empty())
        found(db.exec("SELECT FROM bacuri_maps.map_regions WHERE site_id=$1 FOR SHARE", {site}).size());
    else
        found(db.exec("SELECT FROM bacuri_maps.floor_plans WHERE site_id=$1 AND plan_id=$2 FOR KEY SHARE",
                      {site, plan})
                  .size());
    db.exec("INSERT INTO bacuri_maps.point_placements VALUES ($1,$2,nullif($3,'')::uuid,$4,$5) ON CONFLICT "
            "(site_id,point_id,plan_id) DO UPDATE SET x=excluded.x,y=excluded.y",
            {site, point, plan, body["x"].dump(), body["y"].dump()});
}
} // namespace

void maps(const Request& req, Response& res, const Sessions& sessions, const std::string& site,
          std::string_view path, const std::string& origin)
{
    const bool management = path == "maps/management" || path == "maps/sources" ||
                            path.starts_with("maps/sources/") ||
                            (path == "maps/jobs" || path.starts_with("maps/jobs/"));
    if (management) {
        const bool mutate = req.method() != http::verb::get;
        Database db;
        db.exec("BEGIN");
        auto identity = sessions.authenticate(db, req, mutate, mutate);
        if (access::site_role(db, site, identity.user, access::RoleLock::shared) != "administrator")
            throw Failure{403, "forbidden"};
        map_packages::manage(req, res, db, site, path);
        db.exec("COMMIT");
        return;
    }
    const bool upload = path == "maps/plans", placement = path == "maps/placement", write = upload || placement;
    allow(req, res, {upload ? http::verb::post : placement ? http::verb::put : http::verb::get});
    Database db;
    db.exec(write ? "BEGIN" : "BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY");
    const auto identity = sessions.authenticate(db, req, write, write);
    const auto role =
        access::site_role(db, site, identity.user, write ? access::RoleLock::shared : access::RoleLock::read);
    if (write && role != "administrator")
        throw Failure{403, "forbidden"};
    if (upload)
        respond(res, add_plan(db, req, site), 201);
    else if (placement) {
        place(db, req, site);
        res.result(http::status::no_content);
    } else if (path == "maps")
        respond(res, scene(db, site, identity.user, role == "administrator"));
    else if (path.starts_with("maps/plans/")) {
        const auto id = path.substr(11);
        found(canonical_id(id));
        png(res,
            db.exec(
                "SELECT encode(image,'base64') FROM bacuri_maps.floor_plans p WHERE site_id=$1 AND plan_id=$4 AND "
                "($3::boolean OR EXISTS(SELECT FROM bacuri_maps.point_placements l WHERE l.site_id=p.site_id AND "
                "l.plan_id=p.plan_id AND l.point_id=ANY($2::uuid[])))",
                {site,
                 role == "administrator" ? "{}"
                                         : array_parameter(access::granted_point_ids(db, site, identity.user)),
                 role == "administrator" ? "true" : "false", std::string{id}}));
    } else
        region_resource(db, res, site, path.substr(5), origin);
    db.exec("COMMIT");
}
} // namespace bacuri::vms::control
