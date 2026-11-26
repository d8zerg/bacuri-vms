#include "application/control_api/http_json.hpp"
#include "modules/investigations/case_json.hpp"
#include "modules/investigations/cases.hpp"
#include "modules/investigations/bookmarks.hpp"
#include "modules/recordings/holds.hpp"
#include "modules/access/session.hpp"
#include "modules/access/operations.hpp"
#include "modules/events/operations.hpp"
#include "application/control_api/playback_time_json.hpp"
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
Window material_window(const CaseInterval& interval)
{
    return checked_window({parse_utc(interval.start), parse_utc(interval.end)});
}
struct StoredCase {
    ExportCase file;
    std::string expires_at;
};
bool visible(postgresql::QuerySession& db, const std::string& site, const std::string& user,
             const std::vector<CaseMaterial>& materials)
{
    for (const auto& m : materials)
        if (!recordings::can_view(db, site, user, m.point_id, material_window(m.interval)))
            return false;
    return true;
}
} // namespace
void validate_bookmarks(const std::vector<CaseBookmark>& bookmarks, Window span)
{
    for (const auto& mark : bookmarks) {
        auto at = parse_utc(mark.at);
        require(span.start <= at && at < span.end);
    }
}

std::map<std::string, std::string> eligible_recipients(postgresql::QuerySession& db, const std::string& site,
                                                       const std::string& actor,
                                                       const std::vector<access::MaterialAccess>& spans)
{
    if (spans.empty())
        return {{actor, access::display_name(db, actor)}};
    for (const auto& span : spans) {
        if (!recordings::can_view(db, site, actor, span.point, {span.begin, span.end}) ||
            !access::allowed(db, site, actor, span.point, "protect", "", std::to_string(span.begin),
                             std::to_string(span.end)))
            throw Failure{ErrorCode::forbidden};
    }
    return access::investigation_assignees(db, site, spans);
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
    std::vector<access::MaterialAccess> spans;
    Json out = Json::array();
    for (const auto& m : body.at("materials")) {
        auto point = string_field(m, "point_id", 36);
        if (!canonical_id(point) || !m.contains("interval"))
            throw Failure{ErrorCode::invalid_request};
        auto span = window(m.at("interval"));
        spans.push_back({point, span.start, span.end});
    }
    for (const auto& [id, name] : eligible_recipients(db, site, user, spans))
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
    auto materials = Json::parse(r.get(0, 3)).get<std::vector<CaseMaterial>>();
    if (!visible(db, site, user, materials))
        throw Failure{ErrorCode::forbidden};
    return {id, r.get(0), r.get(0, 1), r.get(0, 2), r.get(0, 4), std::move(materials)};
}
std::string save_case(postgresql::QuerySession& db, const std::string& site, const std::string& actor,
                      const std::string& role, CaseUpdate update, const ExportCase* previous)
{
    require(!update.materials.empty() && update.materials.size() <= 32);
    if (update.revision != (previous ? previous->revision : "0"))
        throw Failure{ErrorCode::revision_conflict};
    access::site_role(db, site, update.assignee, access::RoleLock::shared);
    if (previous)
        for (const auto& old : previous->materials) {
            const auto span = material_window(old.interval);
            if (!access::allowed(db, site, actor, old.point_id, "protect", "", std::to_string(span.start),
                                 std::to_string(span.end)))
                throw Failure{ErrorCode::forbidden};
        }
    std::vector<CaseMaterial> stored;
    stored.reserve(update.materials.size());
    for (auto& material : update.materials) {
        const auto span = material_window(material.interval);
        if (!recordings::can_view(db, site, update.assignee, material.point_id, span))
            throw Failure{ErrorCode::forbidden};
        validate_bookmarks(material.bookmarks, span);
        auto hold = archive::uuid();
        recordings::protect(db, site, actor, span, update.assignee, material.point_id, hold, update.reason,
                            update.expires_us);
        stored.push_back({std::move(material.point_id), std::move(hold), std::move(material.interval),
                          std::move(material.bookmarks)});
    }
    // Acquire replacement holds before releasing the previous material, even if retention elapsed.
    if (previous)
        for (const auto& old : previous->materials)
            recordings::release_hold(db, site, old.hold_id);
    if (db.exec("SELECT FROM bacuri_investigations.investigations WHERE site_id=$1 AND number=$2 AND id<>$3",
                {site, update.number, update.id})
            .size())
        throw Failure{ErrorCode::revision_conflict};
    auto saved = db.exec(
        "INSERT INTO bacuri_investigations.investigations AS c VALUES($1,$2,$3,$4,$5,$6,$7,1) "
        "ON CONFLICT(site_id,id) DO UPDATE SET "
        "number=excluded.number,owner_id=excluded.owner_id,notes=excluded.notes,materials=excluded.materials,"
        "expires_us=excluded.expires_us,revision=c.revision+1 WHERE c.revision=$8::bigint AND (c.owner_id=$9 "
        "OR $10='administrator') RETURNING revision",
        {site, update.id, update.number, update.assignee, update.notes, Json(stored).dump(),
         std::to_string(update.expires_us), update.revision, actor, role});
    if (!saved.size())
        throw Failure{ErrorCode::revision_conflict};
    events::record_recording_change(db, site, stored.front().point_id, actor, "case_change",
                                    update.number + " - " + update.reason);
    return saved.get(0);
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
    std::vector<StoredCase> visible_cases;
    for (int i = 0; i < rows.size(); ++i) {
        auto materials = Json::parse(rows.get(i, 4)).get<std::vector<CaseMaterial>>();
        if (visible(db, site, user, materials))
            visible_cases.push_back(
                {{rows.get(i), rows.get(i, 1), rows.get(i, 2), rows.get(i, 3), rows.get(i, 6),
                  std::move(materials)},
                 ProtoTime::ToString(ProtoTime::MicrosecondsToTimestamp(std::stoll(rows.get(i, 5))))});
    }
    if (!write) {
        if (!id.empty() && visible_cases.empty())
            throw Failure{ErrorCode::not_found};
        Json list = Json::array();
        for (const auto& entry : visible_cases) {
            Json item = entry.file;
            item["expires_at"] = entry.expires_at;
            list.push_back(std::move(item));
        }
        respond(res, id.empty() ? list : list.front());
    } else {
        const auto body =
            object_body(req, {"revision", "number", "assignee", "notes", "materials", "expires_at", "reason"});
        CaseUpdate update{id,
                          string_field(body, "number", 64),
                          string_field(body, "assignee", 36),
                          string_field(body, "notes", 8192, true),
                          string_field(body, "revision", 19),
                          {},
                          recordings::hold_reason(string_field(body, "reason", 512)),
                          recordings::expires(string_field(body, "expires_at", 32))};
        require(canonical_id(update.assignee) && body.contains("materials") && body.at("materials").is_array() &&
                !body.at("materials").empty() && body.at("materials").size() <= 32);
        if (rows.size() && visible_cases.empty())
            throw Failure{ErrorCode::not_found};
        update.materials = body.at("materials").get<std::vector<CaseMaterialInput>>();
        const auto revision = save_case(db, site, user, role, std::move(update),
                                        visible_cases.empty() ? nullptr : &visible_cases.front().file);
        respond(res, {{"id", id}, {"revision", revision}});
    }
    db.exec("COMMIT");
}
} // namespace bacuri::vms::control::investigations
