#include "storage/postgresql/database.hpp"
#include "modules/access/operations.hpp"
#include "application/control_api/api.hpp"

namespace bacuri::vms::control::access
{
std::map<std::string, std::vector<std::string>> group_memberships(postgresql::Database& db,
                                                                  const std::string& site)
{
    auto rows = db.exec("SELECT g.group_id,p.point_id FROM bacuri_access.resource_groups g LEFT JOIN "
                        "bacuri_access.group_points p USING(site_id,group_id) WHERE g.site_id=$1",
                        {site});
    std::map<std::string, std::vector<std::string>> result;
    for (int i = 0; i < rows.size(); ++i) {
        auto& points = result[rows.get(i)];
        if (!rows.get(i, 1).empty())
            points.push_back(rows.get(i, 1));
    }
    return result;
}
void lock_policy(postgresql::Database& db, const std::string& site, bool shared)
{
    db.exec(shared ? "SELECT pg_advisory_xact_lock_shared(hashtextextended($1,41))"
                   : "SELECT pg_advisory_xact_lock(hashtextextended($1,41))",
            {site});
}
std::string scope_revision(postgresql::Database& db, const std::string& site)
{
    return db
        .exec("SELECT coalesce((SELECT revision FROM bacuri_access.policy_versions WHERE site_id=$1),0)", {site})
        .get(0);
}
std::string site_role(postgresql::QuerySession& db, const std::string& site, const std::string& user,
                      RoleLock lock)
{
    auto r = db.exec(
        lock == RoleLock::shared
            ? "SELECT role FROM bacuri_access.site_roles WHERE site_id=$1 AND user_id=$2 AND enabled FOR SHARE"
            : "SELECT role FROM bacuri_access.site_roles WHERE site_id=$1 AND user_id=$2 AND enabled",
        {site, user});
    if (!r.size())
        throw Failure{ErrorCode::not_found};
    return r.get(0);
}
bool lock_point_grant(postgresql::Database& db, const std::string& site, const std::string& point,
                      const std::string& user)
{
    return allowed(db, site, user, point, "view");
}

std::string delivery_regions(postgresql::QuerySession& db, const std::string& site, const std::string& point,
                             const std::string& surface)
{
    const auto rows = db.exec(
        "SELECT regions::text FROM bacuri_access.privacy_policies WHERE site_id=$1 AND point_id=$2 AND CASE $3 "
        "WHEN 'live' THEN live WHEN 'archive' THEN archive WHEN 'thumbnail' THEN thumbnail WHEN 'export' THEN "
        "export ELSE false END AND jsonb_array_length(regions)>0",
        {site, point, surface});
    return rows.size() ? rows.get(0) : "";
}

bool allowed(postgresql::QuerySession& db, const std::string& site, const std::string& user,
             const std::string& point, const std::string& action, const std::string& surface,
             const std::string& begin, const std::string& end)
{
    return db.exec("SELECT bacuri_access.can_use($1,$2,$3,$4,nullif($5,'')::bigint,nullif($6,'')::bigint,$7)",
                   {site, user, point, action, begin, end, surface})
               .get(0) == "t";
}

std::string role_for_existing_point(postgresql::Database& db, const std::string& site, const std::string& point,
                                    const std::string& user)
{
    const auto role = site_role(db, site, user, RoleLock::read);
    if (!allowed(db, site, user, point, "view"))
        throw Failure{ErrorCode::not_found};
    return role;
}
std::vector<std::string> granted_point_ids(postgresql::Database& db, const std::string& site,
                                           const std::string& user, const std::string& action,
                                           bool include_intervals)
{
    return db
        .exec("SELECT DISTINCT point_id FROM bacuri_access.effective_grants WHERE site_id=$1 AND user_id=$2 "
              "AND point_id IS NOT NULL AND action=$3 AND (begin_us IS NULL OR $4::boolean)",
              {site, user, action, include_intervals ? "true" : "false"})
        .column();
}
std::vector<std::string> export_point_ids(postgresql::Database& db, const std::string& site,
                                          const std::string& user)
{
    return granted_point_ids(db, site, user, "export", true);
}
std::vector<std::string> viewer_ids(postgresql::Database& db, const std::string& site, const std::string& point)
{
    return db
        .exec("SELECT user_id FROM bacuri_access.site_roles WHERE site_id=$1 AND "
              "bacuri_access.can_use(site_id,user_id,$2,'view',NULL,NULL,'') ORDER BY user_id",
              {site, point})
        .column();
}
std::map<std::string, std::string> investigation_assignees(postgresql::Database& db, const std::string& site,
                                                           const std::string& spans)
{
    auto rows =
        db.exec("SELECT u.user_id,u.username FROM bacuri_access.local_users u JOIN bacuri_access.site_roles r "
                "USING(user_id) "
                "WHERE r.site_id=$1 AND r.enabled AND NOT EXISTS(SELECT FROM jsonb_to_recordset($2::jsonb) AS "
                "m(point uuid,a bigint,b bigint) "
                "WHERE NOT bacuri_access.can_use(r.site_id,u.user_id,m.point,'view',m.a,m.b,'') OR NOT "
                "bacuri_access.can_use(r.site_id,u.user_id,m.point,'protect',m.a,m.b,'')) ORDER BY u.username",
                {site, spans});
    std::map<std::string, std::string> names;
    for (int i = 0; i < rows.size(); i++)
        names.emplace(rows.get(i), rows.get(i, 1));
    return names;
}
std::string display_name(postgresql::Database& db, const std::string& user)
{
    if (user.empty())
        return {};
    return db.exec("SELECT username FROM bacuri_access.local_users WHERE user_id=$1", {user}).get(0);
}
std::string browser_expiry(postgresql::Database& db, const std::string& browser)
{
    return db
        .exec("SELECT least(expires_at,last_active+interval '30 minutes') FROM bacuri_access.browser_sessions "
              "WHERE token_hash=$1",
              {browser})
        .get(0);
}
std::string export_regions(postgresql::QuerySession& db, const std::string& site, const std::string& point)
{
    auto row = db.exec("SELECT regions::text FROM bacuri_access.privacy_policies WHERE site_id=$1 AND point_id=$2 "
                       "AND (archive OR export)",
                       {site, point});
    if (!row.size())
        return {};
    return Json::parse(row.get(0)).empty() ? R"([{"left":0,"top":0,"right":1,"bottom":1}])" : row.get(0);
}
std::string export_permission(postgresql::QuerySession& db, const std::string& site, const std::string& point,
                              const std::string& user, const std::string& begin, const std::string& end,
                              bool derivative)
{
    if (!allowed(db, site, user, point, "view", "", begin, end) ||
        (!derivative && !export_regions(db, site, point).empty()))
        throw Failure{ErrorCode::forbidden};
    auto rows = db.exec("SELECT grant_id FROM bacuri_access.effective_grants WHERE site_id=$1 AND point_id=$2 "
                        "AND user_id=$3 AND (action='export' OR ($6::boolean AND action='export_masked')) AND "
                        "(begin_us IS NULL OR (begin_us<=$4 AND end_us>=$5)) "
                        "ORDER BY (action='export_masked') DESC,grant_id LIMIT 1",
                        {site, point, user, begin, end, derivative ? "true" : "false"});
    if (!rows.size())
        throw Failure{ErrorCode::forbidden};
    return rows.get(0);
}
std::optional<ExportLease> export_lease(postgresql::Database& db, const std::string& browser,
                                        const std::string& grant, const std::string& point,
                                        const std::string& begin, const std::string& end, bool derivative,
                                        const std::string& regions, bool sound)
{
    auto rows =
        db.exec(R"sql(
        SELECT g.site_id,g.point_id,least(1000,floor(extract(epoch FROM
            least(b.expires_at,b.last_active+interval '30 minutes',b.oidc_until)-clock_timestamp())*1000))::bigint
        FROM bacuri_access.browser_sessions b JOIN bacuri_access.effective_grants g USING(user_id)
        WHERE b.token_hash=$1 AND (b.oidc_site IS NULL OR b.oidc_site=g.site_id) AND g.grant_id=$2 AND g.point_id=$3
            AND (g.action='export' OR ($6::boolean AND g.action='export_masked'))
            AND (g.begin_us IS NULL OR (g.begin_us<=$4 AND g.end_us>=$5))
            AND bacuri_access.can_use(g.site_id,g.user_id,g.point_id,'view',$4,$5,'')
            AND (NOT $7::boolean OR bacuri_access.can_use(g.site_id,g.user_id,g.point_id,'audio',$4,$5,''))
    )sql",
                {browser, grant, point, begin, end, derivative ? "true" : "false", sound ? "true" : "false"});
    if (!rows.size() || export_regions(db, rows.get(0), point) != regions || (!derivative && !regions.empty()))
        return {};
    return ExportLease{rows.get(0), rows.get(0, 1), std::stoll(rows.get(0, 2))};
}

void provision_user(postgresql::Database& db, const std::string& site, const LocalAccount& account,
                    const std::string& password, bool initial)
{
    lock_policy(db, site);
    if (initial) {
        db.exec("SELECT pg_advisory_xact_lock(hashtextextended($1,1))", {site});
        auto existing = db.exec("SELECT username FROM bacuri_access.site_roles JOIN bacuri_access.local_users "
                                "USING(user_id) WHERE site_id=$1 "
                                "AND role='administrator'",
                                {site});
        if (existing.size()) {
            auto same = db.exec("SELECT FROM bacuri_access.site_roles JOIN bacuri_access.local_users "
                                "USING(user_id) WHERE site_id=$1 AND "
                                "username=$2 AND role='administrator'",
                                {site, account.username});
            if (!same.size())
                throw std::invalid_argument("Site already has an administrator");
            return;
        }
    }
    auto user =
        db.exec("INSERT INTO bacuri_access.local_users(username,password_hash) VALUES ($1,$2) RETURNING user_id",
                {account.username, password_hash(password)})
            .get(0);
    db.exec("INSERT INTO bacuri_access.site_roles VALUES ($1,$2,$3)", {user, site, account.role});
}

bool has_authenticator(postgresql::Database& db, const std::string& username)
{
    return db
        .exec("SELECT FROM bacuri_access.webauthn_credentials JOIN bacuri_access.local_users USING(user_id) WHERE "
              "username=$1",
              {username})
        .size();
}

void revoke_export(postgresql::Database& db, const GrantTarget& target)
{
    db.exec("DELETE FROM bacuri_access.export_grants g USING bacuri_access.local_users u WHERE u.username=$1 AND "
            "g.user_id=u.user_id "
            "AND g.site_id=$2 AND g.point_id=$3",
            {target.username, target.site, target.point});
}

void grant_export(postgresql::Database& db, const GrantTarget& target, std::int64_t begin, std::int64_t end)
{
    if (end <= begin)
        throw std::invalid_argument("Invalid export interval");
    auto grant =
        db.exec("INSERT INTO bacuri_access.export_grants(user_id,site_id,point_id,begin_us,end_us) "
                "SELECT r.user_id,r.site_id,$3,$4,$5 FROM bacuri_access.site_roles r JOIN "
                "bacuri_access.local_users u USING(user_id) "
                "WHERE u.username=$1 AND r.site_id=$2 AND "
                "bacuri_access.can_use(r.site_id,r.user_id,$3,'view',$4,$5,'') ON "
                "CONFLICT(user_id,site_id,point_id) "
                "DO UPDATE SET begin_us=excluded.begin_us,end_us=excluded.end_us,grant_id=excluded.grant_id "
                "RETURNING grant_id",
                {target.username, target.site, target.point, std::to_string(begin), std::to_string(end)});
    if (!grant.size())
        throw std::invalid_argument("User has no access to this point");
}

void revoke_point(postgresql::Database& db, const GrantTarget& target)
{
    db.exec("DELETE FROM bacuri_access.point_grants g USING bacuri_access.local_users u WHERE g.user_id=u.user_id "
            "AND u.username=$1 AND "
            "g.site_id=$2 AND g.point_id=$3",
            {target.username, target.site, target.point});
}

void grant_point(postgresql::Database& db, const GrantTarget& target)
{
    db.exec("BEGIN");
    lock_policy(db, target.site);
    auto user = db.exec(R"sql(
            SELECT u.user_id FROM bacuri_access.local_users u JOIN bacuri_access.site_roles r USING(user_id)
            WHERE u.username=$1 AND r.site_id=$2 AND r.role='operator' FOR SHARE
        )sql",
                        {target.username, target.site});
    if (!user.size()) {
        throw std::runtime_error("Operator account not found on this site");
    }
    db.exec("INSERT INTO bacuri_access.point_grants VALUES ($1,$2,$3) ON CONFLICT DO NOTHING",
            {user.get(0), target.site, target.point});
    db.exec("COMMIT");
}

std::optional<std::string> browser_point_expiry(postgresql::Database& db, const std::string& browser,
                                                const std::string& site, const std::string& point,
                                                const std::string& surface, const std::string& begin,
                                                const std::string& end)
{
    auto row =
        db.exec("SELECT least(b.expires_at,b.last_active+interval '30 minutes',b.oidc_until) "
                "FROM bacuri_access.browser_sessions b WHERE token_hash=$1 AND (b.oidc_site IS NULL OR "
                "b.oidc_site=$2) AND "
                "bacuri_access.can_use($2,b.user_id,$3,'view',nullif($4,'')::bigint,nullif($5,'')::bigint,$6)",
                {browser, site, point, begin, end, surface});
    if (!row.size())
        return {};
    return row.get(0);
}

std::map<std::string, std::string> display_names(postgresql::Database& db, const std::vector<std::string>& users)
{
    std::map<std::string, std::string> names;
    if (users.empty())
        return names;
    const auto rows = db.exec("SELECT user_id,username FROM bacuri_access.local_users WHERE user_id IN "
                              "(SELECT value::uuid FROM jsonb_array_elements_text($1::jsonb))",
                              {Json(users).dump()});
    for (int i = 0; i < rows.size(); ++i)
        names.emplace(rows.get(i), rows.get(i, 1));
    return names;
}
} // namespace bacuri::vms::control::access
