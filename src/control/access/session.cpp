#include "control/access/session.hpp"
#include "control/api.hpp"
#include "control/access/webauthn.hpp"

#include <algorithm>

namespace bacuri::vms::control
{
namespace
{
constexpr std::string_view cookie_name = "__Host-bacuri_session=";
constexpr std::string_view attributes = "; Path=/; HttpOnly; Secure; SameSite=Strict";

std::string cookie(const Request& request)
{
    std::string header = std::string{request[http::field::cookie]};
    std::string_view remaining{header};
    while (!remaining.empty()) {
        const auto separator = remaining.find(';');
        auto part = remaining.substr(0, separator);
        part.remove_prefix(std::min(part.size(), part.find_first_not_of(' ')));
        if (part.starts_with(cookie_name)) {
            const auto value = part.substr(cookie_name.size());
            if (value.size() == 64 && value.find_first_not_of("0123456789abcdef") == value.npos) {
                return std::string{value};
            }
        }
        if (separator == remaining.npos) {
            break;
        }
        remaining.remove_prefix(separator + 1);
    }
    return {};
}

Json session_body(Database& db, const Key& key, const Identity& identity)
{
    const auto rows = db.exec(R"sql(
        SELECT to_char(expires_at AT TIME ZONE 'UTC','YYYY-MM-DD"T"HH24:MI:SS.US"Z"'),
            (SELECT coalesce(jsonb_agg(jsonb_build_object('site_id',site_id,'role',role) ORDER BY site_id),'[]')
             FROM bacuri_access.site_roles WHERE user_id=s.user_id AND enabled AND (s.oidc_site IS NULL OR site_id=s.oidc_site))
        FROM bacuri_access.browser_sessions s WHERE token_hash=$1
    )sql",
                              {fingerprint(key, "session", identity.cookie)});
    return {{"user_id", identity.user},
            {"expires_at", rows.get(0)},
            {"site_roles", Json::parse(rows.get(0, 1))},
            {"csrf_token", fingerprint(key, "csrf", identity.cookie)}};
}
} // namespace

void check_origin(const Request& request, const std::string& origin)
{
    if (request.count(http::field::origin) != 1 || request[http::field::origin] != origin) {
        throw Failure{403, "forbidden"};
    }
}

Sessions::Sessions(Key key, std::string origin)
    : key_{key}, origin_{std::move(origin)}, dummy_hash_{password_hash(token())}
{
}

Identity Sessions::authenticate(Database& db, const Request& request, bool mutation, bool activity) const
{
    auto value = cookie(request);
    if (value.empty()) {
        throw Failure{401, "unauthenticated"};
    }
    const auto rows =
        db.exec(activity ? "UPDATE bacuri_access.browser_sessions SET last_active=now() WHERE token_hash=$1 AND "
                           "expires_at>now() "
                           "AND (oidc_until IS NULL OR oidc_until>clock_timestamp()) AND "
                           "last_active>now()-interval '30 minutes' RETURNING user_id,oidc_site"
                         : "SELECT user_id,oidc_site FROM bacuri_access.browser_sessions WHERE token_hash=$1 AND "
                           "expires_at>now() "
                           "AND (oidc_until IS NULL OR oidc_until>clock_timestamp()) AND "
                           "last_active>now()-interval '30 minutes'",
                {fingerprint(key_, "session", value)});
    if (!rows.size()) {
        throw Failure{401, "unauthenticated"};
    }
    const std::string_view target{request.target()};
    constexpr std::string_view sites = "/api/v1/sites/";
    if (!rows.get(0, 1).empty() && target.starts_with(sites) && target.substr(sites.size(), 36) != rows.get(0, 1))
        throw Failure{403, "forbidden"};
    if (mutation) {
        check_origin(request, origin_);
        if (request.count("X-CSRF-Token") != 1 || request["X-CSRF-Token"] != fingerprint(key_, "csrf", value)) {
            throw Failure{403, "forbidden"};
        }
    }
    return {rows.get(0), std::move(value)};
}

void Sessions::login(const Request& request, Response& response, const std::string& peer) const
{
    check_origin(request, origin_);
    const auto body = object_body(request, {"username", "password", "enrollment_code"});
    const auto username = string_field(body, "username", 128);
    const auto password = string_field(body, "password", 1024);
    Database db;
    const auto attempts = db.exec(R"sql(
            INSERT INTO bacuri_access.login_attempts VALUES ($1,now(),1) ON CONFLICT (client_address) DO UPDATE SET
            attempts=CASE WHEN login_attempts.started_at<now()-interval '1 minute' THEN 1 ELSE login_attempts.attempts+1 END,
            started_at=CASE WHEN login_attempts.started_at<now()-interval '1 minute' THEN now() ELSE login_attempts.started_at END
            RETURNING attempts
        )sql",
                                  {peer});
    if (std::stoi(attempts.get(0)) > 12) {
        response.set("Retry-After", "60");
        throw Failure{429, "rate_limited"};
    }
    db.exec("BEGIN");
    auto user = db.exec("SELECT user_id,password_hash FROM bacuri_access.local_users WHERE username=$1 FOR UPDATE",
                        {username});
    if (!password_matches(user.size() ? user.get(0, 1) : dummy_hash_, password) || !user.size()) {
        throw Failure{401, "invalid_credentials"};
    }
    if (!db.exec("SELECT FROM bacuri_access.site_roles WHERE user_id=$1 AND enabled", {user.get(0)}).size())
        throw Failure{401, "invalid_credentials"};
    if (db.exec("SELECT FROM bacuri_access.site_roles WHERE user_id=$1 AND enabled AND role='administrator'",
                {user.get(0)})
            .size()) {
        auto challenge = begin_webauthn(db, key_, origin_, user.get(0), body);
        db.exec("COMMIT");
        return respond(response, challenge, 202);
    }
    issue(db, request, response, user.get(0));
}
void Sessions::factor(const Request& request, Response& response) const
{
    check_origin(request, origin_);
    const auto body =
        object_body(request, {"ticket", "credential_id", "client_data", "authenticator_data", "signature"});
    Database db;
    db.exec("BEGIN");
    const auto ticket = fingerprint(key_, "webauthn", string_field(body, "ticket", 64));
    db.exec("SAVEPOINT verify_factor");
    VerifiedFactor result;
    try {
        result = finish_webauthn(db, key_, origin_, body);
    } catch (const Failure&) {
        db.exec("ROLLBACK TO SAVEPOINT verify_factor");
        db.exec("DELETE FROM bacuri_access.webauthn_challenges WHERE ticket_hash=$1", {ticket});
        db.exec("COMMIT");
        throw;
    }
    if (result.enrolled) {
        db.exec("COMMIT");
        response.result(http::status::no_content);
        return;
    }
    issue(db, request, response, result.user);
}
void Sessions::issue(Database& db, const Request& request, Response& response, const std::string& user_id,
                     const std::string& site, const std::string& refresh) const
{
    auto previous = cookie(request);
    db.exec("DELETE FROM bacuri_access.browser_sessions WHERE token_hash=$1 OR expires_at<=now() OR "
            "last_active<=now()-interval '30 minutes'",
            {fingerprint(key_, "session", previous)});
    const Identity identity{user_id, token()};
    const auto hash = fingerprint(key_, "session", identity.cookie);
    db.exec("INSERT INTO "
            "bacuri_access.browser_sessions(token_hash,user_id,expires_at,login_method,oidc_site,oidc_until,"
            "sealed_refresh_token) "
            "VALUES($1,$2,now()+interval '8 hours',$3,nullif($4,'')::uuid,CASE WHEN $3='oidc' THEN now()+interval "
            "'55 seconds' END,nullif($5,''))",
            {hash, user_id, site.empty() ? "local" : "oidc", site,
             site.empty() ? "" : seal(key_, "oidc-session/" + hash, refresh)});
    db.exec("INSERT INTO bacuri_access.access_audit(site_id,actor,method,action,resource) SELECT "
            "site_id,$1::uuid::text,$2,'session.login',$1::uuid::text "
            "FROM bacuri_access.site_roles WHERE user_id=$1::uuid AND enabled AND ($3='' OR "
            "site_id=nullif($3,'')::uuid)",
            {user_id, site.empty() ? "local" : "oidc", site});
    auto result = session_body(db, key_, identity);
    db.exec("COMMIT");
    response.set("Set-Cookie", std::string{cookie_name} + identity.cookie + std::string{attributes});
    respond(response, result, 201);
}
void Sessions::handle(const Request& request, Response& response, const std::string& peer) const
{
    switch (request.method()) {
    case http::verb::post:
        return login(request, response, peer);
    case http::verb::delete_:
        return logout(request, response);
    case http::verb::get: {
        Database db;
        db.exec("BEGIN");
        auto identity = authenticate(db, request, false, false);
        respond(response, session_body(db, key_, identity));
        db.exec("COMMIT");
        return;
    }
    default:
        response.set(http::field::allow, "GET, POST, DELETE");
        throw Failure{405, "invalid_request"};
    }
}
void Sessions::logout(const Request& request, Response& response) const
{
    check_origin(request, origin_);
    Database db;
    db.exec("BEGIN");
    auto value = cookie(request);
    if (!value.empty()) {
        try {
            auto identity = authenticate(db, request, true);
            db.exec("INSERT INTO bacuri_access.access_audit(site_id,actor,method,action,resource) "
                    "SELECT r.site_id,r.user_id::text,b.login_method,'session.logout',r.user_id::text FROM "
                    "bacuri_access.browser_sessions b JOIN bacuri_access.site_roles r USING(user_id) WHERE "
                    "b.token_hash=$1 AND r.enabled AND (b.oidc_site IS NULL OR b.oidc_site=r.site_id)",
                    {fingerprint(key_, "session", identity.cookie)});
            db.exec("DELETE FROM bacuri_access.browser_sessions WHERE token_hash=$1",
                    {fingerprint(key_, "session", identity.cookie)});
        } catch (const Failure& error) {
            if (error.status != 401) {
                throw;
            }
        }
    }
    db.exec("COMMIT");
    response.set("Set-Cookie", std::string{cookie_name} + "; Max-Age=0" + std::string{attributes});
    response.result(http::status::no_content);
}
} // namespace bacuri::vms::control
