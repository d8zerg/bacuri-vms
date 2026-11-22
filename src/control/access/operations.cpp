#include "control/database.hpp"
#include "control/access/operations.hpp"
#include "control/api.hpp"

namespace bacuri::vms::control::access
{
std::string site_role(Database& db, const std::string& site, const std::string& user, RoleLock lock)
{
    auto r = db.exec(lock == RoleLock::shared
                         ? "SELECT role FROM bacuri_access.site_roles WHERE site_id=$1 AND user_id=$2 FOR SHARE"
                         : "SELECT role FROM bacuri_access.site_roles WHERE site_id=$1 AND user_id=$2",
                     {site, user});
    if (!r.size())
        throw Failure{404, "not_found"};
    return r.get(0);
}
bool lock_point_grant(Database& db, const std::string& site, const std::string& point, const std::string& user)
{
    return db
        .exec("SELECT FROM bacuri_access.point_grants WHERE site_id=$1 AND point_id=$2 AND user_id=$3 FOR SHARE",
              {site, point, user})
        .size();
}
std::string role_for_existing_point(Database& db, const std::string& site, const std::string& point,
                                    const std::string& user)
{
    const auto role = site_role(db, site, user, RoleLock::read);
    if (role == "administrator" ||
        db.exec("SELECT FROM bacuri_access.point_grants WHERE user_id=$1 AND site_id=$2 AND point_id=$3",
                {user, site, point})
            .size())
        return role;
    throw Failure{404, "not_found"};
}
std::vector<std::string> granted_point_ids(Database& db, const std::string& site, const std::string& user)
{
    return db.exec("SELECT point_id FROM bacuri_access.point_grants WHERE site_id=$1 AND user_id=$2", {site, user})
        .column();
}
std::vector<std::string> export_point_ids(Database& db, const std::string& site, const std::string& user)
{
    return db
        .exec("SELECT point_id FROM bacuri_access.export_grants WHERE site_id=$1 AND user_id=$2", {site, user})
        .column();
}
std::vector<std::string> viewer_ids(Database& db, const std::string& site, const std::string& point)
{
    return db
        .exec("SELECT user_id FROM bacuri_access.site_roles r WHERE site_id=$1 AND (role='administrator' OR "
              "EXISTS(SELECT FROM bacuri_access.point_grants g WHERE g.user_id=r.user_id AND g.site_id=r.site_id "
              "AND "
              "g.point_id=$2)) ORDER BY user_id",
              {site, point})
        .column();
}
std::string display_name(Database& db, const std::string& user)
{
    if (user.empty())
        return {};
    return db.exec("SELECT username FROM bacuri_access.local_users WHERE user_id=$1", {user}).get(0);
}
std::string browser_expiry(Database& db, const std::string& browser)
{
    return db
        .exec("SELECT least(expires_at,last_active+interval '30 minutes') FROM bacuri_access.browser_sessions "
              "WHERE token_hash=$1",
              {browser})
        .get(0);
}
namespace
{
constexpr auto point_permission = R"sql(
    (r.role='administrator' OR EXISTS (SELECT FROM bacuri_access.point_grants p
     WHERE (p.site_id,p.user_id,p.point_id)=(r.site_id,r.user_id,g.point_id)))
)sql";
}
std::string export_permission(Database& db, const std::string& site, const std::string& point,
                              const std::string& user, const std::string& begin, const std::string& end)
{
    const auto query =
        std::string{"SELECT grant_id FROM bacuri_access.export_grants g JOIN bacuri_access.site_roles r "
                    "USING(site_id,user_id) "
                    "WHERE user_id=$1 AND site_id=$2 AND point_id=$3 AND begin_us<=$4 AND end_us>=$5 AND "} +
        point_permission;
    auto grant = db.exec(query.c_str(), {user, site, point, begin, end});
    if (!grant.size())
        throw Failure{403, "forbidden"};
    return grant.get(0);
}
std::optional<ExportLease> export_lease(Database& db, const std::string& browser, const std::string& grant)
{
    const auto query =
        std::string{"SELECT g.site_id,g.point_id,least(1000,floor(extract(epoch FROM "
                    "least(b.expires_at,b.last_active+interval '30 minutes')-clock_timestamp())*1000))::bigint "
                    "FROM bacuri_access.browser_sessions b JOIN bacuri_access.export_grants g USING(user_id) JOIN "
                    "bacuri_access.site_roles r USING(user_id,site_id) "
                    "WHERE b.token_hash=$1 AND g.grant_id=$2 AND "} +
        point_permission;
    auto r = db.exec(query.c_str(), {browser, grant});
    if (!r.size())
        return {};
    return ExportLease{r.get(0), r.get(0, 1), std::stoll(r.get(0, 2))};
}

void provision_user(Database& db, const std::string& site, const LocalAccount& account,
                    const std::string& password, bool initial)
{
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

bool has_authenticator(Database& db, const std::string& username)
{
    return db
        .exec("SELECT FROM bacuri_access.webauthn_credentials JOIN bacuri_access.local_users USING(user_id) WHERE "
              "username=$1",
              {username})
        .size();
}

void revoke_export(Database& db, const GrantTarget& target)
{
    db.exec("DELETE FROM bacuri_access.export_grants g USING bacuri_access.local_users u WHERE u.username=$1 AND "
            "g.user_id=u.user_id "
            "AND g.site_id=$2 AND g.point_id=$3",
            {target.username, target.site, target.point});
}

void grant_export(Database& db, const GrantTarget& target, std::int64_t begin, std::int64_t end)
{
    if (end <= begin)
        throw std::invalid_argument("Invalid export interval");
    auto grant =
        db.exec("INSERT INTO bacuri_access.export_grants(user_id,site_id,point_id,begin_us,end_us) "
                "SELECT r.user_id,r.site_id,$3,$4,$5 FROM bacuri_access.site_roles r JOIN "
                "bacuri_access.local_users u USING(user_id) "
                "WHERE u.username=$1 AND r.site_id=$2 AND (r.role='administrator' OR EXISTS(SELECT FROM "
                "bacuri_access.point_grants p "
                "WHERE (p.site_id,p.user_id,p.point_id)=(r.site_id,r.user_id,$3::uuid))) ON "
                "CONFLICT(user_id,site_id,point_id) "
                "DO UPDATE SET begin_us=excluded.begin_us,end_us=excluded.end_us,grant_id=excluded.grant_id "
                "RETURNING grant_id",
                {target.username, target.site, target.point, std::to_string(begin), std::to_string(end)});
    if (!grant.size())
        throw std::invalid_argument("User has no access to this point");
}

void revoke_point(Database& db, const GrantTarget& target)
{
    db.exec("DELETE FROM bacuri_access.point_grants g USING bacuri_access.local_users u WHERE g.user_id=u.user_id "
            "AND u.username=$1 AND "
            "g.site_id=$2 AND g.point_id=$3",
            {target.username, target.site, target.point});
}

void grant_point(Database& db, const GrantTarget& target)
{
    db.exec("BEGIN");
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

std::optional<std::string> browser_point_expiry(Database& db, const std::string& browser, const std::string& site,
                                                const std::string& point)
{
    auto row = db.exec(
        "SELECT least(b.expires_at,b.last_active+interval '30 minutes') FROM bacuri_access.browser_sessions b "
        "JOIN bacuri_access.site_roles r USING(user_id) WHERE b.token_hash=$1 AND r.site_id=$2 AND "
        "(r.role='administrator' OR "
        "EXISTS(SELECT FROM bacuri_access.point_grants p WHERE "
        "(p.site_id,p.user_id,p.point_id)=(r.site_id,r.user_id,$3::uuid)))",
        {browser, site, point});
    if (!row.size())
        return {};
    return row.get(0);
}

std::map<std::string, std::string> display_names(Database& db, const std::vector<std::string>& users)
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
