#include "modules/recordings/policies.hpp"
#include "modules/recordings/operations.hpp"
#include "modules/access/session.hpp"
#include "modules/access/operations.hpp"
#include "modules/events/operations.hpp"
#include "application/control_api/playback_time.hpp"
#include "archive/manifest.hpp"
#include "modules/recordings/holds.hpp"

namespace bacuri::vms::control::recordings
{
namespace
{
void require(bool value)
{
    if (!value)
        throw Failure{ErrorCode::invalid_request};
}
} // namespace
bool can_view(postgresql::Database& db, const std::string& site, const std::string& user, const std::string& point,
              Window span)
{
    auto mask = access::delivery_regions(db, site, point, "archive");
    return access::allowed(db, site, user, point, "view", mask.empty() ? "archive" : "",
                           std::to_string(span.start), std::to_string(span.end));
}
void protect(postgresql::Database& db, const std::string& site, const std::string& actor, Window span,
             const std::string& owner, const std::string& point, const std::string& id, const std::string& reason,
             std::int64_t expiry)
{
    const auto begin = std::to_string(span.start), end = std::to_string(span.end);
    if (!can_view(db, site, actor, point, span) ||
        !access::allowed(db, site, actor, point, "protect", "", begin, end))
        throw Failure{ErrorCode::forbidden};
    if (!db.exec("SELECT FROM bacuri_archive.retained_blocks WHERE site_id=$1 AND point_id=$2 AND "
                 "unavailable_reason IS NULL "
                 "AND int8range(begin_utc_us,end_utc_us,'[)') && int8range($3,$4,'[)') LIMIT 1",
                 {site, point, begin, end})
             .size())
        throw Failure{ErrorCode::media_unavailable};
    auto saved = db.exec("INSERT INTO bacuri_archive.archive_holds VALUES($1,$2,$3,$4,$5,$6,$7,$8) ON CONFLICT DO "
                         "NOTHING RETURNING id",
                         {site, id, point, begin, end, owner, reason, std::to_string(expiry)});
    if (!saved.size())
        throw Failure{ErrorCode::revision_conflict};
    db.exec("INSERT INTO bacuri_archive.held_blocks SELECT site_id,$2,block_id FROM "
            "bacuri_archive.retained_blocks WHERE site_id=$1 AND point_id=$3 "
            "AND int8range(begin_utc_us,end_utc_us,'[)') && int8range($4,$5,'[)')",
            {site, id, point, begin, end});
    events::record_recording_change(db, site, point, actor, "archive_hold", reason);
}
void lock_archive(postgresql::Database& db, const std::string& site)
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
void release_hold(postgresql::Database& db, const std::string& site, const std::string& id)
{
    db.exec("DELETE FROM bacuri_archive.archive_holds WHERE site_id=$1 AND id=$2", {site, id});
}
void holds(const Request& req, Response& res, const Sessions& sessions, const std::string& site,
           const std::string& point)
{
    allow(req, res, {http::verb::get, http::verb::post});
    postgresql::Database db;
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
} // namespace bacuri::vms::control::recordings
