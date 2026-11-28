#include "application/control_api/http_json.hpp"
#include "modules/access/admin.hpp"
#include "modules/access/operations.hpp"
#include "modules/access/oidc.hpp"
#include "modules/inventory/operations.hpp"
#include "application/control_api/playback_time.hpp"
#include <algorithm>
#include <cctype>
#include <set>

namespace bacuri::vms::control::access
{
namespace
{
void require(bool ok)
{
    if (!ok)
        throw Failure{ErrorCode::invalid_request};
}
std::string identifier(const Json& body, const char* key)
{
    auto id = string_field(body, key, 36);
    require(canonical_id(id));
    return id;
}
bool flag(const Json& body, const char* key)
{
    require(body.contains(key) && body.at(key).is_boolean());
    return body.at(key).get<bool>();
}
Json list(const postgresql::Rows& rows)
{
    auto result = Json::array();
    for (const auto& item : rows.column())
        result.push_back(Json::parse(item));
    return result;
}
void member(postgresql::Database& db, const std::string& site, const std::string& user)
{
    if (!db.exec("SELECT FROM bacuri_access.site_roles WHERE site_id=$1 AND user_id=$2 FOR UPDATE", {site, user})
             .size())
        throw Failure{ErrorCode::not_found};
}
void check_point(postgresql::Database& db, const std::string& site, const std::string& point)
{
    if (!inventory::lock_point(db, site, point))
        throw Failure{ErrorCode::not_found};
}
std::string role(const Json& body)
{
    auto value = string_field(body, "role", 16);
    require(value == "administrator" || value == "operator");
    return value;
}
Json users(postgresql::Database& db, const Request& req, const std::string& site, const std::string& id)
{
    if (req.method() == http::verb::get && id.empty())
        return list(db.exec(
            R"sql(SELECT jsonb_build_object('id',r.user_id,'username',u.username,'role',r.role,'enabled',r.enabled,
               'issuer',e.issuer,'subject',e.subject)
            FROM bacuri_access.site_roles r JOIN bacuri_access.local_users u USING(user_id)
            LEFT JOIN bacuri_access.external_identities e USING(site_id,user_id) WHERE site_id=$1 ORDER BY u.username)sql",
            {site}));
    if (req.method() == http::verb::post && id.empty()) {
        auto body = object_body(req, {"username", "password", "role"});
        auto name = string_field(body, "username", 128);
        if (db.exec("SELECT FROM bacuri_access.local_users WHERE username=$1", {name}).size())
            throw Failure{ErrorCode::account_conflict};
        provision_user(db, site, {name, role(body)}, string_field(body, "password", 1024), false);
        return {{"id", db.exec("SELECT user_id FROM bacuri_access.local_users WHERE username=$1", {name}).get(0)}};
    }
    require(req.method() == http::verb::patch && canonical_id(id));
    auto body = object_body(req, {"role", "enabled"});
    auto target = role(body);
    bool enabled = flag(body, "enabled");
    member(db, site, id);
    if ((!enabled || target != "administrator") &&
        !db.exec("SELECT FROM bacuri_access.site_roles WHERE site_id=$1 AND user_id<>$2 AND role='administrator' "
                 "AND enabled",
                 {site, id})
             .size())
        throw Failure{ErrorCode::last_administrator};
    db.exec("UPDATE bacuri_access.site_roles SET role=$3,enabled=$4 WHERE site_id=$1 AND user_id=$2 "
            "AND (role,enabled) IS DISTINCT FROM ($3,$4::boolean)",
            {site, id, target, enabled ? "true" : "false"});
    return {{"id", id}};
}
Json groups(postgresql::Database& db, const Request& req, const std::string& site, const std::string& id)
{
    if (req.method() == http::verb::get && id.empty())
        return list(db.exec(
            R"sql(SELECT jsonb_build_object('id',g.group_id,'name',g.name,'points',coalesce((SELECT jsonb_agg(point_id ORDER BY point_id)
              FROM bacuri_access.group_points m WHERE (m.site_id,m.group_id)=(g.site_id,g.group_id)),'[]'))
            FROM bacuri_access.resource_groups g WHERE site_id=$1 ORDER BY name)sql",
            {site}));
    const bool create = req.method() == http::verb::post && id.empty();
    require(create || (req.method() == http::verb::put && canonical_id(id)));
    auto body = object_body(req, {"name", "points"});
    auto name = string_field(body, "name", 128);
    require(body.contains("points") && body["points"].is_array() && body["points"].size() <= 1000);
    std::set<std::string> points;
    for (const auto& item : body["points"]) {
        require(item.is_string() && canonical_id(item.get_ref<const std::string&>()));
        require(points.insert(item.get<std::string>()).second);
        check_point(db, site, item.get_ref<const std::string&>());
    }
    if (db.exec("SELECT FROM bacuri_access.resource_groups WHERE site_id=$1 AND name=$2 AND group_id IS DISTINCT "
                "FROM nullif($3,'')::uuid",
                {site, name, id})
            .size())
        throw Failure{ErrorCode::group_conflict};
    auto row =
        create
            ? db.exec("INSERT INTO bacuri_access.resource_groups(site_id,name) VALUES($1,$2) RETURNING group_id",
                      {site, name})
            : db.exec("UPDATE bacuri_access.resource_groups SET name=$3 WHERE site_id=$1 AND group_id=$2 "
                      "RETURNING group_id",
                      {site, id, name});
    if (!row.size())
        throw Failure{ErrorCode::not_found};
    auto group = row.get(0);
    db.exec("DELETE FROM bacuri_access.group_points WHERE site_id=$1 AND group_id=$2", {site, group});
    db.exec("INSERT INTO bacuri_access.group_points SELECT $1,$2,value::uuid FROM "
            "jsonb_array_elements_text($3::jsonb)",
            {site, group, body["points"].dump()});
    return {{"id", group}};
}
Json grants(postgresql::Database& db, const Request& req, const std::string& site, const std::string& id)
{
    if (req.method() == http::verb::get && id.empty())
        return list(db.exec(R"sql(SELECT to_jsonb(t) FROM (
            SELECT grant_id AS id,user_id,point_id,group_id,action,begin_us::text,end_us::text FROM bacuri_access.scoped_grants WHERE site_id=$1
            UNION ALL SELECT grant_id,user_id,point_id,NULL,'export',begin_us::text,end_us::text FROM bacuri_access.export_grants WHERE site_id=$1
            UNION ALL SELECT md5(site_id::text||user_id::text||point_id::text)::uuid,user_id,point_id,NULL,'view_ptz',NULL,NULL
            FROM bacuri_access.point_grants WHERE site_id=$1) t ORDER BY user_id,id)sql",
                            {site}));
    require(canonical_id(id));
    if (req.method() == http::verb::delete_) {
        if (!db.exec("DELETE FROM bacuri_access.scoped_grants WHERE site_id=$1 AND grant_id=$2 RETURNING grant_id",
                     {site, id})
                 .size()) {
            auto old = db.exec(
                "SELECT u.username,g.point_id,g.action FROM bacuri_access.effective_grants g "
                "JOIN bacuri_access.local_users u USING(user_id) WHERE g.site_id=$1 AND g.grant_id=$2 LIMIT 1",
                {site, id});
            if (!old.size())
                throw Failure{ErrorCode::not_found};
            GrantTarget target{site, old.get(0), old.get(0, 1)};
            if (old.get(0, 2) == "export")
                revoke_export(db, target);
            else
                revoke_point(db, target);
        }
        return {{"id", id}};
    }
    require(req.method() == http::verb::put);
    auto body = object_body(req, {"user_id", "action", "point_id", "group_id", "interval"});
    auto user = identifier(body, "user_id"), action = string_field(body, "action", 16);
    const std::set<std::string> actions{"view", "audio",     "audio_record", "record", "audio_talk",
                                        "ptz",  "configure", "protect",      "export", "export_masked"};
    require(actions.contains(action) && body.contains("point_id") != body.contains("group_id"));
    member(db, site, user);
    std::string point, group, begin, end;
    if (body.contains("point_id")) {
        point = identifier(body, "point_id");
        check_point(db, site, point);
    } else {
        group = identifier(body, "group_id");
        if (!db.exec("SELECT FROM bacuri_access.resource_groups WHERE site_id=$1 AND group_id=$2", {site, group})
                 .size())
            throw Failure{ErrorCode::not_found};
    }
    if (body.contains("interval")) {
        require(action == "view" || action == "export" || action == "export_masked");
        auto range = body.at("interval");
        auto a = parse_utc(string_field(range, "start", 32)), b = parse_utc(string_field(range, "end", 32));
        require(range.size() == 2 && a < b);
        begin = std::to_string(a);
        end = std::to_string(b);
    }
    if (db.exec("SELECT FROM bacuri_access.export_grants WHERE grant_id=$1 UNION ALL SELECT FROM "
                "bacuri_access.point_grants "
                "WHERE md5(site_id::text||user_id::text||point_id::text)::uuid=$1",
                {id})
            .size())
        throw Failure{ErrorCode::grant_conflict};
    auto row = db.exec(R"sql(INSERT INTO bacuri_access.scoped_grants AS g VALUES($1,$2,$3,nullif($4,'')::uuid,
         nullif($5,'')::uuid,$6,nullif($7,'')::bigint,nullif($8,'')::bigint) ON CONFLICT(grant_id) DO UPDATE SET
         user_id=excluded.user_id,point_id=excluded.point_id,group_id=excluded.group_id,action=excluded.action,
         begin_us=excluded.begin_us,end_us=excluded.end_us WHERE g.site_id=excluded.site_id RETURNING grant_id)sql",
                       {id, site, user, point, group, action, begin, end});
    if (!row.size())
        throw Failure{ErrorCode::grant_conflict};
    return {{"id", id}};
}
Json privacy(postgresql::Database& db, const Request& req, const std::string& site, const std::string& id)
{
    if (req.method() == http::verb::get && id.empty())
        return list(db.exec("SELECT to_jsonb(p)-'site_id' FROM bacuri_access.privacy_policies p WHERE site_id=$1 "
                            "ORDER BY point_id",
                            {site}));
    require(req.method() == http::verb::put && canonical_id(id));
    check_point(db, site, id);
    auto body = object_body(req, {"live", "archive", "thumbnail", "export", "regions"});
    const auto regions = body.value("regions", Json::array());
    require(regions.is_array() && regions.size() <= 32);
    for (const auto& region : regions) {
        require(region.is_object() && region.size() == 4);
        for (const char* edge : {"left", "top", "right", "bottom"}) {
            require(region.contains(edge) && region[edge].is_number() && region[edge] >= 0 && region[edge] <= 1);
        }
        require(region["left"] < region["right"] && region["top"] < region["bottom"]);
    }
    db.exec(
        "INSERT INTO bacuri_access.privacy_policies VALUES($1,$2,$3,$4,$5,$6,$7) ON CONFLICT(site_id,point_id) "
        "DO UPDATE SET "
        "live=excluded.live,archive=excluded.archive,thumbnail=excluded.thumbnail,export=excluded.export,regions="
        "excluded.regions",
        {site, id, flag(body, "live") ? "true" : "false", flag(body, "archive") ? "true" : "false",
         flag(body, "thumbnail") ? "true" : "false", flag(body, "export") ? "true" : "false", regions.dump()});
    return {{"id", id}};
}
} // namespace
bool administration(const Request& req, Response& res, const Sessions& sessions, const Key& key,
                    const std::string& site, std::string_view path)
{
    if (path == "keyboard-profile") {
        allow(req, res, {http::verb::put});
        postgresql::Database db;
        db.exec("BEGIN");
        const auto user = sessions.authenticate(db, req, true).user;
        if (site_role(db, site, user, RoleLock::shared) != "administrator")
            throw Failure{ErrorCode::forbidden};
        const auto body = object_body(req, {"revision", "bindings"});
        const auto revision = string_field(body, "revision", 19);
        require(body.contains("bindings") && body["bindings"].is_object() && body["bindings"].size() <= 128);
        require(revision.find_first_not_of("0123456789") == revision.npos);
        for (const auto& [id, binding] : body["bindings"].items()) {
            require(!id.empty() && id.size() <= 64 &&
                    id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") == id.npos &&
                    binding.is_string());
            auto value = binding.get<std::string>();
            require(!value.empty() && value.size() <= 64 &&
                    std::ranges::all_of(value, [](unsigned char ch) { return ch >= 32 && ch <= 126; }));
            std::ranges::transform(value, value.begin(),
                                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            require(value.find("meta+") == value.npos && value.find("super+") == value.npos &&
                    value != "alt+tab" && value != "alt+shift+tab" && value != "ctrl+alt+t");
        }
        auto updated =
            db.exec("UPDATE bacuri_inventory.sites SET keymap=$2::jsonb,keymap_revision=keymap_revision+1 WHERE "
                    "site_id=$1 AND keymap_revision::text=$3 RETURNING keymap_revision",
                    {site, body["bindings"].dump(), revision});
        if (!updated.size())
            throw Failure{ErrorCode::revision_conflict};
        db.exec("INSERT INTO bacuri_access.access_audit(site_id,actor,method,action,resource) "
                "VALUES($1,$2,$3,'keyboard_profile.publish',$4)",
                {site, user, Sessions::desktop_request(req) ? "desktop" : "browser", updated.get(0)});
        db.exec("COMMIT");
        respond(res, {{"revision", updated.get(0)}});
        return true;
    }
    auto slash = path.find('/');
    auto resource = path.substr(0, slash);
    if (resource != "users" && resource != "resource-groups" && resource != "grants" &&
        resource != "privacy-policies" && path != "identity/provider" && path != "audit/search")
        return false;
    const bool audit = path == "audit/search", write = req.method() != http::verb::get && !audit;
    postgresql::Database db;
    db.exec(write ? "BEGIN" : "BEGIN ISOLATION LEVEL REPEATABLE READ");
    if (write)
        lock_policy(db, site);
    auto who = sessions.authenticate(db, req, req.method() != http::verb::get, write);
    if (site_role(db, site, who.user, RoleLock::shared) != "administrator")
        throw Failure{ErrorCode::forbidden};
    std::string id{slash == path.npos ? std::string_view{} : path.substr(slash + 1)};
    if (write) {
        const auto revision = scope_revision(db, site);
        const auto tag = "\"" + revision + "\"";
        res.set("ETag", tag);
        if (req.count("If-Match") != 1)
            throw Failure{ErrorCode::precondition_required};
        if (req["If-Match"] != tag)
            throw Failure{ErrorCode::precondition_failed};
        db.exec("SELECT set_config('bacuri.actor',$1,true),set_config('bacuri.auth',login_method,true) FROM "
                "bacuri_access.browser_sessions WHERE token_hash=$2",
                {who.user, fingerprint(key, "session", who.cookie)});
    }
    Json value;
    if (path == "identity/provider") {
        if (req.method() == http::verb::get) {
            const auto provider = oidc_provider(db, site);
            value = provider ? Json{{"enabled", provider->enabled},
                                    {"issuer", provider->issuer},
                                    {"client_id", provider->client_id},
                                    {"admin_acr", provider->admin_acr}}
                             : Json{{"enabled", false}};
        } else {
            if (req.method() != http::verb::put)
                throw Failure{ErrorCode::method_not_allowed};
            const auto body = object_body(req, {"enabled", "issuer", "client_id", "admin_acr", "client_secret"});
            require(body.contains("enabled") && body.at("enabled").is_boolean());
            ProviderSettings settings{{body.at("enabled"), string_field(body, "issuer", 1024),
                                       string_field(body, "client_id", 128), string_field(body, "admin_acr", 128)},
                                      {}};
            if (body.contains("client_secret"))
                settings.client_secret = string_field(body, "client_secret", 2048);
            configure_provider(db, key, site, settings);
            value = {{"id", site}};
        }
    } else if (resource == "users" && id.ends_with("/external-identity")) {
        id.resize(id.size() - std::string_view{"/external-identity"}.size());
        require(canonical_id(id));
        if (req.method() == http::verb::delete_)
            bind_identity(db, site, id, std::nullopt);
        else {
            if (req.method() != http::verb::put)
                throw Failure{ErrorCode::method_not_allowed};
            const auto body = object_body(req, {"issuer", "subject"});
            bind_identity(
                db, site, id,
                ExternalIdentity{string_field(body, "issuer", 1024), string_field(body, "subject", 255)});
        }
        value = {{"id", id}};
    } else if (resource == "users")
        value = users(db, req, site, id);
    else if (resource == "resource-groups")
        value = groups(db, req, site, id);
    else if (resource == "grants")
        value = grants(db, req, site, id);
    else if (resource == "privacy-policies")
        value = privacy(db, req, site, id);
    else {
        allow(req, res, {http::verb::post});
        auto query = object_body(req, {"actor", "resource"});
        const auto actor = query.contains("actor") ? string_field(query, "actor", 128) : "";
        const auto target = query.contains("resource") ? string_field(query, "resource", 256) : "";
        value = list(db.exec("SELECT to_jsonb(a)-'site_id' FROM bacuri_access.access_audit a WHERE site_id=$1 "
                             "AND ($2='' OR actor=$2) AND ($3='' OR resource=$3 OR "
                             "$3=ANY(string_to_array(resource,'/'))) ORDER BY recorded_at DESC,id DESC LIMIT 200",
                             {site, actor, target}));
    }
    auto version = scope_revision(db, site);
    db.exec("COMMIT");
    res.set("ETag", "\"" + version + "\"");
    respond(res, {{"revision", version}, {"data", value}});
    return true;
}
} // namespace bacuri::vms::control::access
