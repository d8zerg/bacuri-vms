#include "control/recordings/policies.hpp"
#include "control/recordings/operations.hpp"
#include "control/access/operations.hpp"
#include "control/events/operations.hpp"
#include "control/access/session.hpp"
#include "control/inventory/operations.hpp"
#include "archive/recording_policy.hpp"
#include "media/lease.hpp"
#include "control.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <algorithm>
namespace bacuri::vms::control::recordings
{
namespace
{
void require(bool ok)
{
    if (!ok)
        throw Failure{ErrorCode::invalid_request};
}
void lock(Database& db, const std::string& site)
{
    db.exec("SELECT pg_advisory_xact_lock(hashtextextended($1,1))", {site});
}
std::int64_t clock_us(Database& db)
{
    return std::stoll(db.exec("SELECT (extract(epoch FROM clock_timestamp())*1000000)::bigint").get(0));
}
Json listed(Database& db, const std::string& site)
{
    Json result = Json::array();
    auto rows = db.exec("SELECT "
                        "jsonb_build_object('id',id,'scope',scope,'target',target,'priority',priority,'rule',rule,"
                        "'revision',revision::text,'since',changed_us) FROM bacuri_archive.recording_policies "
                        "WHERE site_id=$1 ORDER BY (scope='point') DESC,priority DESC,id",
                        {site});
    for (auto& s : rows.column())
        result.push_back(Json::parse(s));
    return result;
}
Json effective(Database& db, const std::string& site, const Json& policies)
{
    const auto groups = access::group_memberships(db, site);
    Json plan = Json::object();
    for (const auto& row : policies) {
        auto add = [&](const std::string& point) {
            if (!plan.contains(point))
                plan[point] = {{"id", row.at("id")},
                               {"revision", std::stoll(row.at("revision").get<std::string>())},
                               {"rule", row.at("rule")},
                               {"since", row.at("since")},
                               {"windows", Json::array()}};
        };
        auto target = row.at("target").get<std::string>();
        if (row.at("scope") == "point")
            add(target);
        else if (auto it = groups.find(target); it != groups.end())
            for (const auto& p : it->second)
                add(p);
    }
    return plan;
}
std::string authorize(Database& db, const Request& req, const Sessions& sessions, const std::string& site,
                      bool admin)
{
    access::lock_policy(db, site, true);
    auto user = sessions.authenticate(db, req, req.method() != http::verb::get).user;
    if (access::site_role(db, site, user, access::RoleLock::shared) != "administrator" && admin)
        throw Failure{ErrorCode::forbidden};
    return user;
}
} // namespace
Json recording_plan(Database& db, const std::string& site)
{
    auto plan = effective(db, site, listed(db, site));
    auto windows =
        db.exec("SELECT point_id,policy_id,revision,binding,begin_us,end_us FROM bacuri_archive.recording_windows "
                "WHERE site_id=$1 AND end_us>=bacuri_archive.retention_now_us()-90000000 ORDER BY "
                "point_id,binding,begin_us,end_us",
                {site});
    for (int i = 0; i < windows.size(); ++i) {
        auto p = plan.find(windows.get(i));
        if (p == plan.end() || p->at("id") != windows.get(i, 1) ||
            p->at("revision") != std::stoll(windows.get(i, 2)))
            continue;
        auto& ranges = p->at("windows");
        auto binding = std::stoll(windows.get(i, 3)), begin = std::stoll(windows.get(i, 4)),
             end = std::stoll(windows.get(i, 5));
        if (!ranges.empty() && ranges.back()[0] == binding && ranges.back()[2] >= begin)
            ranges.back()[2] = std::max(end, ranges.back()[2].get<long long>());
        else
            ranges.push_back(Json::array({binding, begin, end}));
    }
    return plan;
}
void advance_point_retention(Database& db, const std::string& site, const Json& plan)
{
    Json days = Json::object();
    for (const auto& [point, p] : plan.items())
        if (!p.at("rule").at("days").is_null())
            days[point] = p.at("rule").at("days");
    db.exec(
        R"sql(UPDATE bacuri_archive.point_retention SET cutoff_us=greatest(cutoff_us,bacuri_archive.retention_now_us()-days*86400000000::bigint),days=NULL
      WHERE site_id=$1 AND days IS NOT NULL AND NOT ($2::jsonb ? point_id::text))sql",
        {site, days.dump()});
    db.exec(
        R"sql(INSERT INTO bacuri_archive.point_retention AS r SELECT $1,key::uuid,value::int,bacuri_archive.retention_now_us()-value::bigint*86400000000::bigint FROM jsonb_each_text($2::jsonb)
      ON CONFLICT(site_id,point_id) DO UPDATE SET cutoff_us=greatest(r.cutoff_us,excluded.cutoff_us,bacuri_archive.retention_now_us()-r.days*86400000000::bigint),days=excluded.days)sql",
        {site, days.dump()});
}
void motion_recording(Database& db, const std::string& site, const std::string& point, std::int64_t binding,
                      std::int64_t at)
{
    db.exec("SELECT pg_advisory_xact_lock_shared(hashtextextended($1,1))", {site});
    auto plan = effective(db, site, listed(db, site));
    auto p = plan.find(point);
    if (p == plan.end() || !p->at("rule").at("event").get<bool>() || at < p->at("since").get<std::int64_t>())
        return;
    const auto& rule = p->at("rule");
    db.exec("INSERT INTO bacuri_archive.recording_windows VALUES($1,$2,$3,$4,$5,'event',$6,$7)",
            {site, point, std::to_string(binding), p->at("id").get<std::string>(), p->at("revision").dump(),
             std::to_string(std::max(p->at("since").get<std::int64_t>(),
                                     at - rule.at("pre").get<int>() * std::int64_t{1000000})),
             std::to_string(at + rule.at("post").get<int>() * std::int64_t{1000000})});
}
void policies(const Request& req, Response& res, const Sessions& sessions, const std::string& site,
              const std::string& id)
{
    if (id.empty())
        allow(req, res, {http::verb::get});
    else
        allow(req, res, {http::verb::get, http::verb::put, http::verb::delete_});
    Database db;
    db.exec("BEGIN");
    const auto user = authorize(db, req, sessions, site, true);
    if (req.method() == http::verb::get) {
        auto list = listed(db, site);
        if (id.empty())
            respond(res, {{"policies", list}, {"effective", effective(db, site, list)}});
        else {
            auto item = std::ranges::find_if(list, [&](const auto& row) { return row.at("id") == id; });
            if (item == list.end())
                throw Failure{ErrorCode::not_found};
            respond(res, *item);
        }
    } else {
        lock(db, site);
        auto current = db.exec("SELECT revision,scope,target FROM bacuri_archive.recording_policies WHERE "
                               "site_id=$1 AND id=$2 FOR UPDATE",
                               {site, id});
        if (req.method() == http::verb::delete_) {
            if (!current.size())
                throw Failure{ErrorCode::not_found};
            std::string tag{"\""};
            tag += current.get(0);
            tag += '"';
            if (req["If-Match"] != tag)
                throw Failure{ErrorCode::revision_conflict};
            db.exec("DELETE FROM bacuri_archive.recording_policies WHERE site_id=$1 AND id=$2", {site, id});
            events::record_recording_change(db, site, current.get(0, 1) == "point" ? current.get(0, 2) : "", user,
                                            "recording_policy", "policy_deleted",
                                            {{"policy", id}, {"revision", current.get(0)}});
            res.result(http::status::no_content);
        } else {
            auto body = object_body(req, {"scope", "target", "priority", "rule", "revision"});
            auto scope = string_field(body, "scope", 8), target = string_field(body, "target", 36),
                 revision = string_field(body, "revision", 19);
            require((scope == "point" || scope == "group") && canonical_id(target) && body.contains("priority") &&
                    body["priority"].is_number_integer() && body["priority"] >= 0 && body["priority"] <= 100 &&
                    body.contains("rule"));
            try {
                archive::parse_capture_rule(body.at("rule"));
            } catch (const std::exception&) {
                throw Failure{ErrorCode::invalid_request};
            }
            if (revision != (current.size() ? current.get(0) : "0"))
                throw Failure{ErrorCode::revision_conflict};
            if (scope == "point" ? !inventory::point_exists(db, site, target)
                                 : !access::group_memberships(db, site).contains(target))
                throw Failure{ErrorCode::not_found};
            if (db.exec("SELECT FROM bacuri_archive.recording_policies WHERE site_id=$1 AND scope=$2 AND "
                        "target=$3 AND id<>$4",
                        {site, scope, target, id})
                    .size())
                throw Failure{ErrorCode::revision_conflict};
            auto saved = db.exec(
                "INSERT INTO bacuri_archive.recording_policies(site_id,id,scope,target,priority,rule,changed_us) "
                "VALUES($1,$2,$3,$4,$5,$6,$7) ON CONFLICT(site_id,id) DO UPDATE SET "
                "scope=excluded.scope,target=excluded.target,priority=excluded.priority,rule=excluded.rule,"
                "revision=recording_policies.revision+1,changed_us=excluded.changed_us RETURNING revision",
                {site, id, scope, target, body["priority"].dump(), body["rule"].dump(),
                 std::to_string(clock_us(db))});
            events::record_recording_change(db, site, scope == "point" ? target : "", user, "recording_policy",
                                            "policy_saved", {{"policy", id}, {"revision", saved.get(0)}});
            respond(res, {{"id", id}, {"revision", saved.get(0)}});
        }
        advance_point_retention(db, site, effective(db, site, listed(db, site)));
    }
    db.exec("COMMIT");
}
void manual_recording(const Request& req, Response& res, const Sessions& sessions, const std::string& site,
                      const std::string& point)
{
    allow(req, res, {http::verb::get, http::verb::put});
    Database db;
    db.exec("BEGIN");
    auto user = authorize(db, req, sessions, site, false);
    if (!access::allowed(db, site, user, point, "view", "live") ||
        !access::allowed(db, site, user, point, "record"))
        throw Failure{ErrorCode::forbidden};
    lock(db, site);
    auto plan = effective(db, site, listed(db, site));
    auto p = plan.find(point);
    auto bindings = inventory::recording_bindings(db, site);
    const bool enabled = p != plan.end() && p->at("rule").at("manual").get<bool>() && bindings.contains(point);
    auto stamp = clock_us(db);
    std::int64_t until = 0;
    if (enabled) {
        std::string id = p->at("id"), revision = p->at("revision").dump(),
                    binding = std::to_string(bindings.at(point));
        auto current =
            db.exec("SELECT max(end_us) FROM bacuri_archive.recording_windows WHERE site_id=$1 AND point_id=$2 "
                    "AND policy_id=$3 AND revision=$4 AND binding=$5 AND kind='manual' AND end_us>$6",
                    {site, point, id, revision, binding, std::to_string(stamp)});
        if (!current.get(0).empty())
            until = std::stoll(current.get(0));
        if (req.method() == http::verb::put) {
            auto b = object_body(req, {"active", "seconds"});
            require(b.contains("active") && b["active"].is_boolean());
            auto seconds = b.contains("seconds") ? b["seconds"] : Json(900);
            require(seconds.is_number_integer() && seconds >= 1 && seconds <= 3600);
            if (b.at("active") == true && p->at("rule").at("audio") == true &&
                !access::allowed(db, site, user, point, "audio_record", "live"))
                throw Failure{ErrorCode::forbidden};
            if (b.at("active") == true && !until) {
                until = stamp + seconds.get<int>() * std::int64_t{1000000};
                db.exec("INSERT INTO bacuri_archive.recording_windows VALUES($1,$2,$3,$4,$5,'manual',$6,$7)",
                        {site, point, binding, id, revision, std::to_string(stamp), std::to_string(until)});
                events::record_recording_change(db, site, point, user, "manual_recording",
                                                "manual_recording_started", {{"seconds", seconds}});
            } else if (b.at("active") == false && until) {
                db.exec("UPDATE bacuri_archive.recording_windows SET end_us=greatest(begin_us,$6) WHERE "
                        "site_id=$1 AND point_id=$2 AND policy_id=$3 AND revision=$4 AND binding=$5 AND "
                        "kind='manual' AND end_us>$6",
                        {site, point, id, revision, binding, std::to_string(stamp)});
                events::record_recording_change(db, site, point, user, "manual_recording",
                                                "manual_recording_stopped");
                until = 0;
            }
        }
    } else if (req.method() == http::verb::put)
        throw Failure{ErrorCode::manual_recording_disabled};
    db.exec("COMMIT");
    respond(res,
            {{"enabled", enabled}, {"active", until > stamp}, {"until_us", until ? Json(until) : Json(nullptr)}});
}
void volumes(const Request& req, Response& res, const Sessions& sessions, const std::string& site)
{
    allow(req, res, {http::verb::get, http::verb::put});
    Database db;
    db.exec("BEGIN");
    const auto user = authorize(db, req, sessions, site, true);
    if (req.method() == http::verb::put) {
        lock(db, site);
        auto b = object_body(req, {"days", "reserve_percent", "revision"});
        auto revision = string_field(b, "revision", 19);
        auto current =
            db.exec("SELECT revision FROM bacuri_archive.archive_retention WHERE site_id=$1 FOR UPDATE", {site});
        if (revision != (current.size() ? current.get(0) : "0"))
            throw Failure{ErrorCode::revision_conflict};
        require(b.contains("days") && b["days"].is_number_integer() && b["days"] >= 1 && b["days"] <= 36500 &&
                b.contains("reserve_percent") && b["reserve_percent"].is_number_integer() &&
                b["reserve_percent"] >= 1 && b["reserve_percent"] <= 90);
        change_retention(db, site, b.at("days"), b.at("reserve_percent"));
        events::record_recording_change(db, site, "", user, "recording_storage", "storage_changed",
                                        {{"days", b.at("days")}, {"reserve_percent", b.at("reserve_percent")}});
    }
    auto config = db.exec(
        "SELECT days,reserve_percent,revision FROM bacuri_archive.archive_retention WHERE site_id=$1", {site});
    auto depths = db.exec("SELECT point_id,min(begin_utc_us),max(end_utc_us) FROM bacuri_archive.retained_blocks "
                          "WHERE site_id=$1 AND unavailable_reason IS NULL GROUP BY point_id ORDER BY point_id",
                          {site});
    Json points = Json::array();
    for (int i = 0; i < depths.size(); ++i)
        points.push_back({{"point", depths.get(i)},
                          {"oldest_us", std::stoll(depths.get(i, 1))},
                          {"newest_us", std::stoll(depths.get(i, 2))}});
    db.exec("COMMIT");
    v1::RetentionQuery q;
    q.set_site_id(site);
    q.set_recording_node_id(setting("BACURI_NODE_ID"));
    auto recorder = v1::RecorderControl::NewStub(media::local_channel("BACURI_SOCKET"));
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds{2});
    v1::StorageStatus out;
    auto status = recorder->GetStorageStatus(&ctx, q, &out);
    respond(res, {{"revision", config.size() ? config.get(0, 2) : "0"},
                  {"days", config.size() ? Json(std::stoi(config.get(0))) : Json(nullptr)},
                  {"reserve_percent", config.size() ? std::stoi(config.get(0, 1)) : 20},
                  {"volume", status.ok() ? Json::parse(out.status_json()) : Json(nullptr)},
                  {"points", points}});
}
} // namespace bacuri::vms::control::recordings
