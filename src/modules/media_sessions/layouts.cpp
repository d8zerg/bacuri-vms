#include "application/control_api/http_json.hpp"
#include "modules/access/operations.hpp"
#include "modules/inventory/operations.hpp"
#include "modules/media_sessions/playback.hpp"
#include "modules/media_sessions/layouts.hpp"
#include "storage/postgresql/configuration_snapshot.hpp"
#include "modules/media_sessions/operations.hpp"
#include <set>

namespace bacuri::vms::control
{
void media_sessions::layout_configuration(postgresql::ConfigurationSnapshot& snapshot)
{
    snapshot.section("bacuri_media_sessions.monitor_layouts");
}

std::vector<std::string> media_sessions::layout_points(std::vector<std::string> slots,
                                                       const PointVisibility& visible)
{
    std::set<std::string> unique;
    for (const auto& point : slots) {
        if (!canonical_id(point))
            throw Failure{ErrorCode::invalid_request};
        if (!unique.insert(point).second)
            throw Failure{ErrorCode::invalid_request};
        if (!visible(point))
            throw Failure{ErrorCode::not_found};
    }
    return slots;
}

void Playback::layouts(const Request& req, Response& res, const std::string& site, const std::string& id) const
{
    if (id.empty())
        allow(req, res, {http::verb::get});
    else
        allow(req, res, {http::verb::put, http::verb::delete_});
    postgresql::Database db;
    db.exec("BEGIN");
    const auto user = sessions_.authenticate(db, req, !id.empty()).user;
    const bool admin = access::site_role(db, site, user, access::RoleLock::read) == "administrator";
    if (id.empty()) {
        auto rows = db.exec("SELECT id,name,shared,revision,to_json(points),owner_id=$2 FROM "
                            "bacuri_media_sessions.monitor_layouts WHERE site_id=$1 AND (shared OR owner_id=$2) "
                            "ORDER BY shared,name,id",
                            {site, user});
        Json list = Json::array();
        std::map<std::string, bool> visible;
        for (int i = 0; i < rows.size(); ++i) {
            auto points = Json::parse(rows.get(i, 4));
            for (auto& p : points) {
                const auto point = p.get<std::string>();
                if (!visible.contains(point))
                    visible[point] = access::allowed(db, site, user, point, "view");
                if (!visible.at(point))
                    p = nullptr;
            }
            list.push_back({{"id", rows.get(i)},
                            {"name", rows.get(i, 1)},
                            {"shared", rows.get(i, 2) == "t"},
                            {"revision", rows.get(i, 3)},
                            {"points", points},
                            {"editable", rows.get(i, 2) == "t" ? admin : rows.get(i, 5) == "t"}});
        }
        respond(res, list);
    } else {
        db.exec("SELECT pg_advisory_xact_lock(hashtextextended($1,5))", {site});
        const auto current = db.exec("SELECT owner_id,shared,revision FROM bacuri_media_sessions.monitor_layouts "
                                     "WHERE site_id=$1 AND id=$2 FOR UPDATE",
                                     {site, id});
        if (current.size() && (current.get(0, 1) == "t" ? !admin : current.get(0) != user))
            throw Failure{ErrorCode::not_found};
        if (req.method() == http::verb::delete_) {
            if (!current.size())
                throw Failure{ErrorCode::not_found};
            std::string tag{"\""};
            tag += current.get(0, 2);
            tag += '"';
            if (req["If-Match"] != tag)
                throw Failure{ErrorCode::revision_conflict};
            db.exec("DELETE FROM bacuri_media_sessions.monitor_layouts WHERE site_id=$1 AND id=$2", {site, id});
            res.result(http::status::no_content);
        } else {
            auto body = object_body(req, {"revision", "name", "shared", "points"});
            auto revision = string_field(body, "revision", 19);
            auto name = string_field(body, "name", 320);
            if (g_utf8_strlen(name.c_str(), static_cast<gssize>(name.size())) > 80 ||
                name.find_first_not_of(" \t\r\n") == name.npos || !body.contains("shared") ||
                !body.at("shared").is_boolean() || !body.contains("points") || !body.at("points").is_array() ||
                body.at("points").empty() || body.at("points").size() > 16)
                throw Failure{ErrorCode::invalid_request};
            if (body.at("shared") == true && !admin)
                throw Failure{ErrorCode::forbidden};
            if (revision != (current.size() ? current.get(0, 2) : "0"))
                throw Failure{ErrorCode::revision_conflict};
            if (!std::ranges::all_of(body.at("points"), [](const auto& item) { return item.is_string(); }))
                throw Failure{ErrorCode::invalid_request};
            const auto points = media_sessions::layout_points(
                body.at("points").get<std::vector<std::string>>(), [&](const std::string& point) {
                    return inventory::point_exists(db, site, point) &&
                           access::allowed(db, site, user, point, "view");
                });
            if (!current.size() && db.exec("SELECT count(*)>=128 FROM bacuri_media_sessions.monitor_layouts WHERE "
                                           "site_id=$1 AND owner_id=$2",
                                           {site, user})
                                           .get(0) == "t")
                throw Failure{ErrorCode::rate_limited};
            auto saved = db.exec(
                "INSERT INTO bacuri_media_sessions.monitor_layouts(id,site_id,owner_id,name,shared,points) "
                "VALUES($1,$2,$3,$4,$5,$6) ON CONFLICT(id) DO UPDATE SET "
                "owner_id=CASE WHEN monitor_layouts.shared AND NOT excluded.shared THEN excluded.owner_id ELSE "
                "monitor_layouts.owner_id END, "
                "name=excluded.name,shared=excluded.shared,points=excluded.points,revision=monitor_layouts."
                "revision+1 WHERE monitor_layouts.site_id=excluded.site_id RETURNING revision",
                {id, site, user, name, body.at("shared").dump(), postgresql::array_parameter(points)});
            if (!saved.size())
                throw Failure{ErrorCode::revision_conflict};
            respond(res, {{"id", id}, {"revision", saved.get(0)}});
        }
    }
    db.exec("COMMIT");
}
} // namespace bacuri::vms::control
