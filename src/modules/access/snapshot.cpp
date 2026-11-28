#include "modules/access/admin.hpp"
#include "modules/access/operations.hpp"
#include "application/control_api/http_json.hpp"

namespace bacuri::vms::control::access
{
void snapshot_receipt(const Request& req, Response& res, const Sessions& sessions, const std::string& site,
                      const std::string& point)
{
    allow(req, res, {http::verb::post});
    const std::string capture{req["Idempotency-Key"]};
    if (!canonical_id(capture) || !canonical_id(point))
        throw Failure{ErrorCode::invalid_request};
    const auto body = object_body(req, {"access_revision", "pts_ns", "width", "height", "sha256_rgba8"});
    const auto revision = string_field(body, "access_revision", 19), pts = string_field(body, "pts_ns", 20),
               hash = string_field(body, "sha256_rgba8", 64);
    if (pts.find_first_not_of("0123456789") != pts.npos || hash.size() != 64 ||
        hash.find_first_not_of("0123456789abcdef") != hash.npos)
        throw Failure{ErrorCode::invalid_request};
    for (const auto* field : {"width", "height"})
        if (!body.contains(field) || !body[field].is_number_integer() || body[field] < 1 || body[field] > 65535)
            throw Failure{ErrorCode::invalid_request};
    postgresql::Database db;
    db.exec("BEGIN");
    const auto user = sessions.authenticate(db, req, true).user;
    lock_policy(db, site);
    site_role(db, site, user, RoleLock::read);
    if (!allowed(db, site, user, point, "view"))
        throw Failure{ErrorCode::not_found};
    if (!allowed(db, site, user, point, "export", "export"))
        throw Failure{ErrorCode::forbidden};
    if (scope_revision(db, site) != revision)
        throw Failure{ErrorCode::revision_conflict};
    const auto resource = point + "/" + body.dump();
    auto saved =
        db.exec("INSERT INTO bacuri_access.access_audit(site_id,actor,method,action,resource,idempotency_key) "
                "VALUES($1,$2,$3,'snapshot.capture',$4,$5) ON CONFLICT ON CONSTRAINT access_audit_idempotency "
                "DO NOTHING RETURNING id",
                {site, user, Sessions::desktop_request(req) ? "desktop" : "browser", resource, capture});
    if (!saved.size())
        saved = db.exec("SELECT id FROM bacuri_access.access_audit WHERE site_id=$1 AND actor=$2 AND "
                        "action='snapshot.capture' AND idempotency_key=$3 AND resource=$4",
                        {site, user, capture, resource});
    if (!saved.size())
        throw Failure{ErrorCode::revision_conflict};
    const std::string receipt = saved.get(0);
    db.exec("COMMIT");
    respond(res, {{"receipt", receipt}});
}
} // namespace bacuri::vms::control::access
