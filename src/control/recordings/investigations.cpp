#include "control/recordings/policies.hpp"
#include "control/recordings/operations.hpp"
#include "control/access/session.hpp"
#include "control/access/operations.hpp"
#include "control/events/operations.hpp"
#include "control/playback_time.hpp"
#include "archive/manifest.hpp"
#include <set>

namespace bacuri::vms::control::recordings
{
namespace
{
void require(bool value)
{
    if (!value)
        throw Failure{400, "invalid_request"};
}
bool can_view(Database& db, const std::string& site, const std::string& user, const std::string& point,
              Window span)
{
    auto mask = access::delivery_regions(db, site, point, "archive");
    return access::allowed(db, site, user, point, "view", mask.empty() ? "archive" : "",
                           std::to_string(span.start), std::to_string(span.end));
}
void protect(Database& db, const std::string& site, const std::string& actor, Window span,
             const std::string& owner, const std::string& point, const std::string& id, const std::string& reason,
             std::int64_t expiry)
{
    const auto begin = std::to_string(span.start), end = std::to_string(span.end);
    if (!can_view(db, site, actor, point, span) ||
        !access::allowed(db, site, actor, point, "protect", "", begin, end))
        throw Failure{403, "forbidden"};
    if (!db.exec("SELECT FROM bacuri_archive.retained_blocks WHERE site_id=$1 AND point_id=$2 AND "
                 "unavailable_reason IS NULL "
                 "AND int8range(begin_utc_us,end_utc_us,'[)') && int8range($3,$4,'[)') LIMIT 1",
                 {site, point, begin, end})
             .size())
        throw Failure{409, "media_unavailable"};
    auto saved = db.exec("INSERT INTO bacuri_archive.archive_holds VALUES($1,$2,$3,$4,$5,$6,$7,$8) ON CONFLICT DO "
                         "NOTHING RETURNING id",
                         {site, id, point, begin, end, owner, reason, std::to_string(expiry)});
    if (!saved.size())
        throw Failure{409, "revision_conflict"};
    db.exec("INSERT INTO bacuri_archive.held_blocks SELECT site_id,$2,block_id FROM "
            "bacuri_archive.retained_blocks WHERE site_id=$1 AND point_id=$3 "
            "AND int8range(begin_utc_us,end_utc_us,'[)') && int8range($4,$5,'[)')",
            {site, id, point, begin, end});
    events::record_recording_change(db, site, point, actor, "archive_hold", reason);
}
void lock_archive(Database& db, const std::string& site)
{
    db.exec("SELECT pg_advisory_xact_lock(hashtextextended($1,1))", {site});
    require_available(db, site);
}
std::int64_t expires(const Json& body)
{
    auto expiry = parse_utc(string_field(body, "expires_at", 32));
    require(expiry > g_get_real_time());
    return expiry;
}
std::string reason_field(const Json& body)
{
    auto value = string_field(body, "reason", 512);
    require(value.find_first_not_of(" \t\r\n") != value.npos);
    return value;
}
bool visible(Database& db, const std::string& site, const std::string& user, const Json& materials)
{
    for (const auto& m : materials)
        if (!can_view(db, site, user, m.at("point_id"), window(m.at("interval"))))
            return false;
    return true;
}
} // namespace
void holds(const Request& req, Response& res, const Sessions& sessions, const std::string& site,
           const std::string& point)
{
    allow(req, res, {http::verb::get, http::verb::post});
    Database db;
    db.exec("BEGIN");
    access::lock_policy(db, site, true);
    const bool write = req.method() == http::verb::post;
    const auto user = sessions.authenticate(db, req, write).user;
    access::site_role(db, site, user, access::RoleLock::shared);
    lock_archive(db, site);
    if (write) {
        auto body = object_body(req, {"interval", "reason", "expires_at"});
        require(body.contains("interval"));
        const auto id = archive::uuid();
        protect(db, site, user, window(body.at("interval")), user, point, id, reason_field(body), expires(body));
        respond(res, {{"id", id}}, 201);
    } else {
        auto rows = db.exec("SELECT id,begin_us,end_us,owner_id,reason,expires_us FROM "
                            "bacuri_archive.archive_holds WHERE site_id=$1 AND point_id=$2 "
                            "AND expires_us>bacuri_archive.retention_now_us() ORDER BY expires_us,id",
                            {site, point});
        Json list = Json::array();
        for (int i = 0; i < rows.size(); ++i) {
            Window span{std::stoll(rows.get(i, 1)), std::stoll(rows.get(i, 2))};
            if (can_view(db, site, user, point, span) &&
                access::allowed(db, site, user, point, "protect", "", rows.get(i, 1), rows.get(i, 2)))
                list.push_back({{"id", rows.get(i)},
                                {"interval", span.json()},
                                {"owner", rows.get(i, 3)},
                                {"reason", rows.get(i, 4)},
                                {"expires_at", ProtoTime::ToString(ProtoTime::MicrosecondsToTimestamp(
                                                   std::stoll(rows.get(i, 5))))}});
        }
        respond(res, list);
    }
    db.exec("COMMIT");
}
void case_recipients(const Request& req, Response& res, const Sessions& sessions, const std::string& site)
{
    allow(req, res, {http::verb::post});
    Database db;
    db.exec("BEGIN ISOLATION LEVEL REPEATABLE READ");
    auto user = sessions.authenticate(db, req, true).user;
    access::site_role(db, site, user, access::RoleLock::read);
    auto body = object_body(req, {"materials"});
    if (!body.contains("materials") || !body.at("materials").is_array() || body.at("materials").size() > 32)
        throw Failure{400, "invalid_request"};
    Json spans = Json::array(), out = Json::array();
    for (const auto& m : body.at("materials")) {
        auto point = string_field(m, "point_id", 36);
        if (!canonical_id(point) || !m.contains("interval"))
            throw Failure{400, "invalid_request"};
        auto span = window(m.at("interval"));
        if (!can_view(db, site, user, point, span) ||
            !access::allowed(db, site, user, point, "protect", "", std::to_string(span.start),
                             std::to_string(span.end)))
            throw Failure{403, "forbidden"};
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
Json case_for_export(Database& db, const std::string& site, const std::string& id, const std::string& user)
{
    auto role = access::site_role(db, site, user, access::RoleLock::read);
    auto r = db.exec("SELECT number,owner_id,notes,materials,revision FROM bacuri_archive.investigations WHERE "
                     "site_id=$1 AND id=$2 AND (owner_id=$3 OR $4='administrator')",
                     {site, id, user, role});
    if (!r.size())
        throw Failure{404, "not_found"};
    auto materials = Json::parse(r.get(0, 3));
    if (!visible(db, site, user, materials))
        throw Failure{403, "forbidden"};
    return {{"id", id},
            {"number", r.get(0)},
            {"assignee", r.get(0, 1)},
            {"notes", r.get(0, 2)},
            {"materials", materials},
            {"revision", r.get(0, 4)}};
}
void investigation(const Request& req, Response& res, const Sessions& sessions, const std::string& site,
                   const std::string& id)
{
    if (id.empty())
        allow(req, res, {http::verb::get});
    else
        allow(req, res, {http::verb::get, http::verb::put});
    Database db;
    db.exec("BEGIN");
    access::lock_policy(db, site, true);
    const bool write = req.method() == http::verb::put;
    const auto user = sessions.authenticate(db, req, write).user;
    const auto role = access::site_role(db, site, user, access::RoleLock::shared);
    if (write)
        lock_archive(db, site);
    const auto rows =
        db.exec("SELECT id,number,owner_id,notes,materials,expires_us,revision FROM bacuri_archive.investigations "
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
            throw Failure{404, "not_found"};
        respond(res, id.empty() ? list : list.front());
    } else {
        auto body =
            object_body(req, {"revision", "number", "assignee", "notes", "materials", "expires_at", "reason"});
        auto revision = string_field(body, "revision", 19), number = string_field(body, "number", 64),
             owner = string_field(body, "assignee", 36), notes = string_field(body, "notes", 8192, true),
             reason = reason_field(body);
        require(canonical_id(owner) && body.contains("materials") && body.at("materials").is_array() &&
                !body.at("materials").empty() && body.at("materials").size() <= 32);
        auto expiry = expires(body);
        if (rows.size() && list.empty())
            throw Failure{404, "not_found"};
        if (revision != (list.empty() ? "0" : list.front().at("revision").get<std::string>()))
            throw Failure{409, "revision_conflict"};
        access::site_role(db, site, owner, access::RoleLock::shared);
        if (!list.empty())
            for (const auto& old : list.front().at("materials")) {
                auto span = window(old.at("interval"));
                if (!access::allowed(db, site, user, old.at("point_id"), "protect", "", std::to_string(span.start),
                                     std::to_string(span.end)))
                    throw Failure{403, "forbidden"};
            }
        auto materials = body.at("materials");
        for (auto& m : materials) {
            require(m.is_object() && m.size() == 3 && m.contains("interval") && m.contains("bookmarks") &&
                    m.at("bookmarks").is_array() && m.at("bookmarks").size() <= 64);
            const auto point = string_field(m, "point_id", 36);
            require(canonical_id(point));
            const auto span = window(m.at("interval"));
            if (!can_view(db, site, owner, point, span))
                throw Failure{403, "forbidden"};
            for (const auto& mark : m.at("bookmarks")) {
                require(mark.is_object() && mark.size() == 2);
                auto at = parse_utc(string_field(mark, "at", 32));
                require(span.start <= at && at < span.end);
                string_field(mark, "name", 128);
            }
            const auto hold = archive::uuid();
            protect(db, site, user, span, owner, point, hold, reason, expiry);
            m["hold_id"] = hold;
        }
        // Acquire replacement holds before releasing the previous material, even if retention elapsed.
        if (!list.empty())
            for (const auto& old : list.front().at("materials"))
                db.exec("DELETE FROM bacuri_archive.archive_holds WHERE site_id=$1 AND id=$2",
                        {site, old.at("hold_id")});
        if (db.exec("SELECT FROM bacuri_archive.investigations WHERE site_id=$1 AND number=$2 AND id<>$3",
                    {site, number, id})
                .size())
            throw Failure{409, "revision_conflict"};
        auto saved = db.exec(
            "INSERT INTO bacuri_archive.investigations AS c VALUES($1,$2,$3,$4,$5,$6,$7,1) "
            "ON CONFLICT(site_id,id) DO UPDATE SET "
            "number=excluded.number,owner_id=excluded.owner_id,notes=excluded.notes,materials=excluded.materials,"
            "expires_us=excluded.expires_us,revision=c.revision+1 WHERE c.revision=$8::bigint AND (c.owner_id=$9 "
            "OR $10='administrator') RETURNING revision",
            {site, id, number, owner, notes, materials.dump(), std::to_string(expiry), revision, user, role});
        if (!saved.size())
            throw Failure{409, "revision_conflict"};
        events::record_recording_change(db, site, materials.front().at("point_id"), user, "case_change",
                                        number + " - " + reason);
        respond(res, {{"id", id}, {"revision", saved.get(0)}});
    }
    db.exec("COMMIT");
}
} // namespace bacuri::vms::control::recordings
