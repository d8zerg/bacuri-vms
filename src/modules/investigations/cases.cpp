#include "modules/investigations/cases.hpp"
#include "modules/investigations/bookmarks.hpp"
#include "modules/recordings/holds.hpp"
#include "modules/access/session.hpp"
#include "modules/access/operations.hpp"
#include "modules/events/operations.hpp"
#include "application/control_api/playback_time.hpp"
#include "archive/manifest.hpp"
#include "storage/postgresql/configuration_snapshot.hpp"

namespace bacuri::vms::control::investigations
{
void case_configuration(postgresql::ConfigurationSnapshot& snapshot)
{
    snapshot.section("bacuri_investigations.investigations");
}
namespace
{
void require(bool value)
{
    if (!value)
        throw Failure{ErrorCode::invalid_request};
}
bool visible(postgresql::QuerySession& db, const std::string& site, const std::string& user, const Json& materials)
{
    for (const auto& m : materials)
        if (!recordings::can_view(db, site, user, m.at("point_id"), window(m.at("interval"))))
            return false;
    return true;
}
} // namespace
void validate_bookmarks(const Json& bookmarks, Window span)
{
    for (const auto& mark : bookmarks) {
        require(mark.is_object() && mark.size() == 2);
        auto at = parse_utc(string_field(mark, "at", 32));
        require(span.start <= at && at < span.end);
        string_field(mark, "name", 128);
    }
}

void case_recipients(const Request& req, Response& res, const Sessions& sessions, const std::string& site)
{
    allow(req, res, {http::verb::post});
    postgresql::Database db;
    db.exec("BEGIN ISOLATION LEVEL REPEATABLE READ");
    auto user = sessions.authenticate(db, req, true).user;
    access::site_role(db, site, user, access::RoleLock::read);
    auto body = object_body(req, {"materials"});
    if (!body.contains("materials") || !body.at("materials").is_array() || body.at("materials").size() > 32)
        throw Failure{ErrorCode::invalid_request};
    Json spans = Json::array(), out = Json::array();
    for (const auto& m : body.at("materials")) {
        auto point = string_field(m, "point_id", 36);
        if (!canonical_id(point) || !m.contains("interval"))
            throw Failure{ErrorCode::invalid_request};
        auto span = window(m.at("interval"));
        if (!recordings::can_view(db, site, user, point, span) ||
            !access::allowed(db, site, user, point, "protect", "", std::to_string(span.start),
                             std::to_string(span.end)))
            throw Failure{ErrorCode::forbidden};
        spans.push_back({{"point", point}, {"a", span.start}, {"b", span.end}});
    }
    if (spans.empty())
        out.push_back({{"id", user}, {"name", access::display_name(db, user)}});
    else
        for (const auto& [id, name] : access::investigation_assignees(db, site, spans.dump()))
            out.push_back({{"id", id}, {"name", name}});
    db.exec("COMMIT");
    respond(res, out);
}
ExportCase case_for_export(postgresql::QuerySession& db, const std::string& site, const std::string& id,
                           const std::string& user)
{
    auto role = access::site_role(db, site, user, access::RoleLock::read);
    auto r =
        db.exec("SELECT number,owner_id,notes,materials,revision FROM bacuri_investigations.investigations WHERE "
                "site_id=$1 AND id=$2 AND (owner_id=$3 OR $4='administrator')",
                {site, id, user, role});
    if (!r.size())
        throw Failure{ErrorCode::not_found};
    auto materials = Json::parse(r.get(0, 3));
    if (!visible(db, site, user, materials))
        throw Failure{ErrorCode::forbidden};
    ExportCase result{id, r.get(0), r.get(0, 1), r.get(0, 2), r.get(0, 4), {}};
    for (const auto& item : materials) {
        const auto& interval = item.at("interval");
        CaseMaterial material{
            item.at("point_id"), item.at("hold_id"), {interval.at("start"), interval.at("end")}, {}};
        for (const auto& mark : item.at("bookmarks"))
            material.bookmarks.push_back({mark.at("at"), mark.at("name")});
        result.materials.push_back(std::move(material));
    }
    return result;
}
void investigation(const Request& req, Response& res, const Sessions& sessions, const std::string& site,
                   const std::string& id)
{
    if (id.empty())
        allow(req, res, {http::verb::get});
    else
        allow(req, res, {http::verb::get, http::verb::put});
    postgresql::Database db;
    db.exec("BEGIN");
    access::lock_policy(db, site, true);
    const bool write = req.method() == http::verb::put;
    const auto user = sessions.authenticate(db, req, write).user;
    const auto role = access::site_role(db, site, user, access::RoleLock::shared);
    if (write)
        recordings::lock_archive(db, site);
    const auto rows = db.exec(
        "SELECT id,number,owner_id,notes,materials,expires_us,revision FROM bacuri_investigations.investigations "
        "WHERE site_id=$1 "
        "AND ($2='' OR id=nullif($2,'')::uuid) AND (owner_id=$3 OR $4='administrator') ORDER BY number,id",
        {site, id, user, role});
    Json list = Json::array();
    for (int i = 0; i < rows.size(); ++i) {
        auto materials = Json::parse(rows.get(i, 4));
        if (visible(db, site, user, materials))
            list.push_back({{"id", rows.get(i)},
                            {"number", rows.get(i, 1)},
                            {"assignee", rows.get(i, 2)},
                            {"notes", rows.get(i, 3)},
                            {"materials", materials},
                            {"expires_at",
                             ProtoTime::ToString(ProtoTime::MicrosecondsToTimestamp(std::stoll(rows.get(i, 5))))},
                            {"revision", rows.get(i, 6)}});
    }
    if (!write) {
        if (!id.empty() && list.empty())
            throw Failure{ErrorCode::not_found};
        respond(res, id.empty() ? list : list.front());
    } else {
        auto body =
            object_body(req, {"revision", "number", "assignee", "notes", "materials", "expires_at", "reason"});
        auto revision = string_field(body, "revision", 19), number = string_field(body, "number", 64),
             owner = string_field(body, "assignee", 36), notes = string_field(body, "notes", 8192, true),
             reason = recordings::reason_field(body);
        require(canonical_id(owner) && body.contains("materials") && body.at("materials").is_array() &&
                !body.at("materials").empty() && body.at("materials").size() <= 32);
        auto expiry = recordings::expires(body);
        if (rows.size() && list.empty())
            throw Failure{ErrorCode::not_found};
        if (revision != (list.empty() ? "0" : list.front().at("revision").get<std::string>()))
            throw Failure{ErrorCode::revision_conflict};
        access::site_role(db, site, owner, access::RoleLock::shared);
        if (!list.empty())
            for (const auto& old : list.front().at("materials")) {
                auto span = window(old.at("interval"));
                if (!access::allowed(db, site, user, old.at("point_id"), "protect", "", std::to_string(span.start),
                                     std::to_string(span.end)))
                    throw Failure{ErrorCode::forbidden};
            }
        auto materials = body.at("materials");
        for (auto& m : materials) {
            require(m.is_object() && m.size() == 3 && m.contains("interval") && m.contains("bookmarks") &&
                    m.at("bookmarks").is_array() && m.at("bookmarks").size() <= 64);
            const auto point = string_field(m, "point_id", 36);
            require(canonical_id(point));
            const auto span = window(m.at("interval"));
            if (!recordings::can_view(db, site, owner, point, span))
                throw Failure{ErrorCode::forbidden};
            validate_bookmarks(m.at("bookmarks"), span);
            const auto hold = archive::uuid();
            recordings::protect(db, site, user, span, owner, point, hold, reason, expiry);
            m["hold_id"] = hold;
        }
        // Acquire replacement holds before releasing the previous material, even if retention elapsed.
        if (!list.empty())
            for (const auto& old : list.front().at("materials"))
                recordings::release_hold(db, site, old.at("hold_id"));
        if (db.exec("SELECT FROM bacuri_investigations.investigations WHERE site_id=$1 AND number=$2 AND id<>$3",
                    {site, number, id})
                .size())
            throw Failure{ErrorCode::revision_conflict};
        auto saved = db.exec(
            "INSERT INTO bacuri_investigations.investigations AS c VALUES($1,$2,$3,$4,$5,$6,$7,1) "
            "ON CONFLICT(site_id,id) DO UPDATE SET "
            "number=excluded.number,owner_id=excluded.owner_id,notes=excluded.notes,materials=excluded.materials,"
            "expires_us=excluded.expires_us,revision=c.revision+1 WHERE c.revision=$8::bigint AND (c.owner_id=$9 "
            "OR $10='administrator') RETURNING revision",
            {site, id, number, owner, notes, materials.dump(), std::to_string(expiry), revision, user, role});
        if (!saved.size())
            throw Failure{ErrorCode::revision_conflict};
        events::record_recording_change(db, site, materials.front().at("point_id"), user, "case_change",
                                        number + " - " + reason);
        respond(res, {{"id", id}, {"revision", saved.get(0)}});
    }
    db.exec("COMMIT");
}
} // namespace bacuri::vms::control::investigations
