#include "control/events/workflow.hpp"
#include "control/inventory/operations.hpp"
#include "control/access/operations.hpp"
#include "control/events/alert_work.hpp"
#include <limits>

namespace bacuri::vms::control
{
namespace
{
struct Actor {
    std::string user, delegation, point, action;
};

void require(bool condition)
{
    if (!condition)
        throw Failure{ErrorCode::invalid_request};
}
std::string uuid_field(const Json& body, const char* name)
{
    auto value = string_field(body, name, 36);
    require(canonical_id(value));
    return value;
}
int integer_field(const Json& body, const char* name, int minimum, int maximum)
{
    const auto it = body.find(name);
    require(it != body.end() && it->is_number_integer() && *it >= minimum && *it <= maximum);
    return it->get<int>();
}
const events::WorkAction& work_action(const Json& body)
{
    const auto name = string_field(body, "action", 16);
    for (const auto& action : events::work_actions)
        if (action.name == name)
            return action;
    throw Failure{ErrorCode::invalid_request};
}
events::AlertWork work_state(const Rows& row)
{
    return {row.get(0, 1), row.get(0, 3), std::stoi(row.get(0, 2)), !row.get(0, 4).empty()};
}
std::string role(Database& db, const std::string& site, const std::string& user)
{
    return access::site_role(db, site, user, access::RoleLock::shared);
}
bool point_access(Database& db, const std::string& site, const std::string& user, const std::string& point,
                  bool admin)
{
    return admin ? inventory::lock_point(db, site, point) : access::lock_point_grant(db, site, point, user);
}
Json objects(const Rows& rows)
{
    auto list = Json::array();
    for (int i = 0; i < rows.size(); ++i)
        list.push_back(Json::parse(rows.get(i)));
    return list;
}
constexpr std::string_view audit_fields =
    R"sql(jsonb_build_object('id',event_id,'alert_id',alert_id,'point_id',e.point_id,
    'initiator_id',initiator_id,'actor_kind',e.actor_kind,'delegation_id',e.delegation_id,
    'service',d.service_name,'delegated_action',d.action,'expires_at',
    to_char(d.expires_at AT TIME ZONE 'UTC','YYYY-MM-DD"T"HH24:MI:SS.US"Z"'),
    'action',e.action,'outcome',outcome,'reason',reason,'owner',e.owner_id,
    'from_state',from_state,'to_state',to_state,'version',version,
    'recorded_at',to_char(recorded_at AT TIME ZONE 'UTC','YYYY-MM-DD"T"HH24:MI:SS.US"Z"')))sql";
Json audit_items(Database& db, const Rows& rows, int limit)
{
    auto items = Json::array();
    std::vector<std::string> ids;
    for (int i = 0; i < std::min(rows.size(), limit); ++i) {
        auto item = Json::parse(rows.get(i));
        ids.push_back(item.at("initiator_id").get<std::string>());
        if (!item.at("owner").is_null())
            ids.push_back(item.at("owner").get<std::string>());
        items.push_back(std::move(item));
    }
    const auto names = access::display_names(db, ids);
    for (auto& item : items) {
        item["initiator"] = names.at(item.at("initiator_id").get<std::string>());
        if (!item.at("owner").is_null())
            item["owner"] = names.at(item.at("owner").get<std::string>());
    }
    return items;
}
Actor command_actor(Database& db, const Sessions& sessions, const Key& key, const Request& req,
                    const std::string& site)
{
    if (!req.count(http::field::authorization))
        return {sessions.authenticate(db, req, true).user, {}, {}, {}};
    const std::string_view auth = req[http::field::authorization];
    if (req.count(http::field::authorization) != 1 || req.count(http::field::cookie) ||
        !auth.starts_with("Bearer ") || auth.size() != 71)
        throw Failure{ErrorCode::unauthenticated};
    auto row = db.exec("SELECT issuer_id,delegation_id,point_id,action FROM bacuri_events.alert_delegations WHERE "
                       "site_id=$1 AND token_hash=$2 FOR SHARE",
                       {site, fingerprint(key, "alert-delegation", auth.substr(7))});
    if (!row.size())
        throw Failure{ErrorCode::unauthenticated};
    return {row.get(0), row.get(0, 1), row.get(0, 2), row.get(0, 3)};
}

struct Audit {
    std::string site, point, alert, action, request_key;
    Actor actor;
    std::string append(Database& db, const std::string& outcome, const std::string& reason,
                       const std::string& from = {}, const std::string& to = {},
                       const std::string& version = {}) const
    {
        return db
            .exec(R"sql(
            INSERT INTO bacuri_events.alert_audit
            (site_id,point_id,alert_id,initiator_id,delegation_id,actor_kind,action,outcome,reason,from_state,to_state,owner_id,version,request_key)
            VALUES ($1,nullif($2,'')::uuid,nullif($3,'')::uuid,$4,nullif($5,'')::uuid,$6,$7,$8,$9,nullif($10,''),nullif($11,''),
                    CASE WHEN $11 IN ('in_progress','closed') THEN $4::uuid END,nullif($12,'')::integer,nullif($13,'')::uuid)
            RETURNING event_id
        )sql",
                  {site, point, alert, actor.user, actor.delegation,
                   actor.delegation.empty() || action == "delegate" || action == "revoke" ? "user" : "service",
                   action, outcome, reason, from, to, version, request_key})
            .get(0);
    }
    [[noreturn]] void reject(Database& db, ErrorCode error) const
    {
        append(db, info(error).http == 409 ? "conflict" : "denied", std::string{info(error).code}.c_str());
        db.exec("COMMIT");
        throw Failure{error};
    }
};

void command(Database& db, Response& res, const Request& req, const Sessions& sessions, const Key& key,
             const std::string& site, const std::string& id)
{
    const auto actor = command_actor(db, sessions, key, req, site);
    const bool admin = role(db, site, actor.user) == "administrator";
    const auto body = object_body(req, {"action", "version", "reason"});
    const auto& transition = work_action(body);
    const std::string action{transition.name};
    const auto expected = integer_field(body, "version", 0, std::numeric_limits<int>::max() - 1);
    const auto reason = string_field(body, "reason", 1024);
    require(reason.find_first_not_of(" \t\n\r") != reason.npos);
    const std::string request_key{req["Idempotency-Key"]};
    require(req.count("Idempotency-Key") == 1 && canonical_id(request_key));
    Audit audit{site, {}, id, action, request_key, actor};
    // Serialize reuse of a command key, including concurrent requests for different alerts.
    db.exec("SELECT pg_advisory_xact_lock(hashtextextended($1,0))",
            {site + actor.user + actor.delegation + request_key});
    auto row = db.exec("SELECT point_id,work_state,work_version,owner_id,work_result FROM bacuri_events.alerts "
                       "WHERE site_id=$1 AND alert_id=$2 FOR UPDATE",
                       {site, id});
    if (!row.size())
        audit.reject(db, ErrorCode::not_found);
    audit.point = row.get(0);
    if (!point_access(db, site, actor.user, audit.point, admin))
        audit.reject(db, ErrorCode::not_found);
    if (!actor.delegation.empty()) {
        if (actor.point != audit.point || actor.action != action ||
            !db.exec("SELECT FROM bacuri_events.alert_delegations WHERE delegation_id=$1 AND revoked_at IS NULL "
                     "AND expires_at>clock_timestamp()",
                     {actor.delegation})
                 .size())
            audit.reject(db, ErrorCode::forbidden);
    }
    const auto replay = db.exec(R"sql(
        SELECT event_id,version,alert_id,action,reason FROM bacuri_events.alert_audit
        WHERE site_id=$1 AND initiator_id=$2 AND delegation_id IS NOT DISTINCT FROM nullif($3,'')::uuid
        AND request_key=$4 AND outcome='applied'
    )sql",
                                {site, actor.user, actor.delegation, request_key});
    if (replay.size()) {
        if (replay.get(0, 2) != id || replay.get(0, 3) != action || replay.get(0, 4) != reason ||
            std::stoi(replay.get(0, 1)) != expected + 1)
            audit.reject(db, ErrorCode::idempotency_conflict);
        respond(res, {{"event_id", replay.get(0)}, {"version", expected + 1}});
        return;
    }
    const auto work = work_state(row);
    switch (work.evaluate(transition, actor.user, expected)) {
    case events::WorkDecision::state_conflict:
        audit.reject(db, ErrorCode::alert_state_conflict);
    case events::WorkDecision::different_owner:
        audit.reject(db, ErrorCode::forbidden);
    case events::WorkDecision::missing_result:
        audit.reject(db, ErrorCode::alert_result_required);
    case events::WorkDecision::allowed:
        break;
    }
    const std::string next{transition.to};
    db.exec(
        "UPDATE bacuri_events.alerts SET work_version=work_version+1,owner_id=$3,work_state=$4,work_result=CASE "
        "WHEN $5='record_result' THEN $6 ELSE work_result END WHERE site_id=$1 AND alert_id=$2",
        {site, id, actor.user, next, action, reason});
    const auto event = audit.append(db, "applied", reason, work.state, next, std::to_string(expected + 1));
    db.exec(R"sql(
        INSERT INTO bacuri_events.access_versions(user_id,site_id,revision,work_revision)
        SELECT user_id,$1::uuid,0,1 FROM unnest($2::uuid[]) AS u(user_id) ORDER BY user_id
        ON CONFLICT(user_id,site_id) DO UPDATE SET work_revision=bacuri_events.access_versions.work_revision+1
    )sql",
            {site, array_parameter(access::viewer_ids(db, site, audit.point))});
    respond(res, {{"event_id", event}, {"version", expected + 1}});
}

Json detail(Database& db, const std::string& site, const Identity& viewer, const std::string& id)
{
    const auto& user = viewer.user;
    auto row = db.exec(R"sql(
        SELECT point_id,work_state,work_version,owner_id,work_result FROM bacuri_events.alerts
        WHERE site_id=$1 AND alert_id=$2
    )sql",
                       {site, id});
    if (!row.size())
        throw Failure{ErrorCode::not_found};
    access::role_for_existing_point(db, site, row.get(0), user);
    auto actions = Json::array();
    const auto work = work_state(row);
    for (const auto& action : events::work_actions)
        if (work.evaluate(action, user, work.version) == events::WorkDecision::allowed)
            actions.push_back(action.name);
    const auto history_query =
        "SELECT " + std::string{audit_fields} +
        " FROM bacuri_events.alert_audit e LEFT JOIN bacuri_events.alert_delegations d USING(delegation_id) "
        "WHERE e.site_id=$1 AND e.alert_id=$2 AND outcome='applied' AND version IS NOT NULL ORDER BY version DESC "
        "LIMIT 25";
    const auto history = db.exec(history_query.c_str(), {site, id});
    const auto grants = db.exec(R"sql(
        SELECT jsonb_build_object('id',delegation_id,'service',service_name,'action',action,'expires_at',
                                  to_char(expires_at AT TIME ZONE 'UTC','YYYY-MM-DD"T"HH24:MI:SS.US"Z"'))
        FROM bacuri_events.alert_delegations WHERE site_id=$1 AND point_id=$2 AND issuer_id=$3
        AND revoked_at IS NULL AND expires_at>clock_timestamp() ORDER BY expires_at,delegation_id
    )sql",
                                {site, row.get(0), user});
    auto match = db.exec(
        "SELECT m.revision,m.binding,m.context->>'source',m.preset_state FROM bacuri_events.video_matches m JOIN "
        "bacuri_events.alerts a USING(site_id,source_id,observation_id) WHERE a.site_id=$1 AND a.alert_id=$2",
        {site, id});
    Json details = {
        {"alert_id", id},
        {"point_id", row.get(0)},
        {"state", row.get(0, 1)},
        {"version", std::stoi(row.get(0, 2))},
        {"owner", row.get(0, 3).empty() ? Json(nullptr) : Json(access::display_name(db, row.get(0, 3)))},
        {"result", row.get(0, 4)},
        {"actions", actions},
        {"history", audit_items(db, history, 25)},
        {"delegations", objects(grants)}};
    if (match.size())
        details["rule"] = {{"revision", match.get(0)},
                           {"binding", match.get(0, 1)},
                           {"source", match.get(0, 2)},
                           {"reaction", match.get(0, 3)}};
    return details;
}

Json search(Database& db, const Request& req, const std::string& site, const std::string& user, bool admin)
{
    const auto body = object_body(req, {"query", "alert_id", "before"});
    const auto query = body.contains("query") ? string_field(body, "query", 256, true) : "";
    const auto id = body.contains("alert_id") ? uuid_field(body, "alert_id") : "";
    const auto before = body.contains("before") ? uuid_field(body, "before") : "";
    const auto sql =
        std::string{R"sql(
        WITH accessible AS NOT MATERIALIZED (
          SELECT e.* FROM bacuri_events.alert_audit e
          WHERE e.site_id=$1 AND ($6::boolean OR
              ((e.outcome='applied' OR e.initiator_id=$2) AND e.point_id=ANY($7::uuid[])))
        )
        SELECT )sql"} +
        std::string{audit_fields} +
        R"sql( || CASE WHEN export_details IS NULL THEN '{}'::jsonb ELSE jsonb_build_object('export',export_details) END
               || CASE WHEN e.details IS NULL THEN '{}'::jsonb ELSE jsonb_build_object('details',e.details) END,
               event_id FROM accessible e LEFT JOIN bacuri_events.alert_delegations d USING(delegation_id) WHERE ($3='' OR alert_id=nullif($3,'')::uuid)
        AND (strpos(lower(reason),lower($4))>0 OR export_details->>'id'=$4 OR EXISTS (
            SELECT 1 FROM jsonb_each_text(e.details) detail WHERE strpos(lower(detail.value),lower($4))>0))
        AND ($5='' OR (recorded_at,event_id)<(
            SELECT recorded_at,event_id FROM accessible WHERE event_id=nullif($5,'')::uuid))
        ORDER BY recorded_at DESC,event_id DESC LIMIT 51
    )sql";
    auto rows = db.exec(sql.c_str(), {site, user, id, query, before, admin ? "true" : "false",
                                      admin ? "{}" : array_parameter(access::granted_point_ids(db, site, user))});
    Json page{{"items", audit_items(db, rows, 50)}};
    if (rows.size() > 50)
        page["next_cursor"] = rows.get(49, 1);
    return page;
}

void issue(Database& db, Response& res, const Request& req, const Key& key, const std::string& site,
           const std::string& user)
{
    auto body = object_body(req, {"point_id", "service", "action", "ttl_seconds"});
    const auto point = uuid_field(body, "point_id"), name = string_field(body, "service", 128),
               action = std::string{work_action(body).name};
    const auto ttl = integer_field(body, "ttl_seconds", 1, 86400);
    require(name.find_first_not_of(" \t\r\n") != name.npos);
    if (!point_access(db, site, user, point, role(db, site, user) == "administrator"))
        Audit{site, point, {}, "delegate", {}, {user, {}, {}, {}}}.reject(db, ErrorCode::not_found);
    auto secret = token();
    auto row = db.exec(
        R"sql(
      INSERT INTO bacuri_events.alert_delegations(site_id,point_id,issuer_id,service_name,action,token_hash,expires_at)
      VALUES ($1,$2,$3,$4,$5,$6,clock_timestamp()+$7::integer*interval '1 second')
      RETURNING delegation_id,to_char(expires_at AT TIME ZONE 'UTC','YYYY-MM-DD"T"HH24:MI:SS.US"Z"')
    )sql",
        {site, point, user, name, action, fingerprint(key, "alert-delegation", secret), std::to_string(ttl)});
    Audit{site, point, {}, "delegate", {}, {user, row.get(0), {}, {}}}.append(db, "applied", action);
    respond(res, {{"id", row.get(0)}, {"token", secret}, {"expires_at", row.get(0, 1)}}, 201);
}
void revoke(Database& db, Response& res, const std::string& site, const std::string& user, const std::string& id)
{
    const auto admin = role(db, site, user) == "administrator";
    auto row = db.exec("SELECT point_id,issuer_id,revoked_at IS NULL FROM bacuri_events.alert_delegations WHERE "
                       "site_id=$1 AND delegation_id=$2 FOR UPDATE",
                       {site, id});
    if (!row.size() || (!admin && row.get(0, 1) != user))
        Audit{site, {}, {}, "revoke", {}, {user, {}, {}, {}}}.reject(db, ErrorCode::not_found);
    if (row.get(0, 2) == "t") {
        db.exec("UPDATE bacuri_events.alert_delegations SET revoked_at=clock_timestamp() WHERE delegation_id=$1",
                {id});
        Audit{site, row.get(0), {}, "revoke", {}, {user, id, {}, {}}}.append(db, "applied", "revoked");
    }
    res.result(http::status::no_content);
}
} // namespace

void alert_work(const Request& req, Response& res, const Sessions& sessions, const Key& key,
                const std::string& site, std::string_view path)
{
    require(std::string_view{req.target()}.find('?') == std::string_view::npos);
    if (req.count(http::field::authorization) &&
        !(path.starts_with("alerts/") && path.ends_with("/work") && req.method() == http::verb::post))
        throw Failure{ErrorCode::unauthenticated};
    Database db;
    const bool read = req.method() == http::verb::get;
    db.exec(read || path == "alert-audit/search" ? "BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY" : "BEGIN");
    if (!read && path != "alert-audit/search")
        access::lock_policy(db, site, true);
    if (path.starts_with("alerts/") && path.size() == 48 && path.ends_with("/work") &&
        canonical_id(path.substr(7, 36))) {
        const std::string id{path.substr(7, 36)};
        allow(req, res, {http::verb::get, http::verb::post});
        if (read)
            respond(res, detail(db, site, sessions.authenticate(db, req, false, false), id));
        else
            command(db, res, req, sessions, key, site, id);
    } else if (path == "alert-audit/search") {
        allow(req, res, {http::verb::post});
        const auto user = sessions.authenticate(db, req, true, false).user;
        const bool admin = access::site_role(db, site, user, access::RoleLock::read) == "administrator";
        respond(res, search(db, req, site, user, admin));
    } else if (path == "alert-delegations") {
        allow(req, res, {http::verb::post});
        issue(db, res, req, key, site, sessions.authenticate(db, req, true).user);
    } else if (path.starts_with("alert-delegations/") &&
               canonical_id(path.substr(std::string_view{"alert-delegations/"}.size()))) {
        allow(req, res, {http::verb::delete_});
        revoke(db, res, site, sessions.authenticate(db, req, true).user,
               std::string{path.substr(std::string_view{"alert-delegations/"}.size())});
    } else
        throw Failure{ErrorCode::not_found};
    db.exec("COMMIT");
}
} // namespace bacuri::vms::control
