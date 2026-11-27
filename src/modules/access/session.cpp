#include "modules/access/session.hpp"
#include "application/control_api/http_json.hpp"
#include "modules/access/desktop.hpp"
#include "modules/access/webauthn.hpp"

#include "platform/settings.hpp"
#include <algorithm>

namespace bacuri::vms::control
{
void to_json(Json& j, const FactorChallenge& c)
{
    j = {{"challenge", c.challenge},
         {"ticket", c.ticket},
         {"rp_id", c.rp_id},
         {"user_id", c.user_id},
         {"kind", c.kind}};
    if (c.credential_id)
        j["credential_id"] = *c.credential_id;
}
void from_json(const Json& j, FactorResponse& f)
{
    f = {string_field(j, "ticket", 64),
         string_field(j, "client_data", 8000),
         string_field(j, "authenticator_data", 16000),
         string_field(j, "credential_id", 1500),
         {}};
    if (j.contains("signature"))
        f.signature = string_field(j, "signature", 1024);
}

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

// The token of a desktop session presented in the Authorization header.
std::optional<std::string> desktop_bearer(const Request& request)
{
    if (request.count(http::field::authorization) != 1)
        return std::nullopt;
    return desktop::session_token(request[http::field::authorization]);
}

Json session_body(postgresql::Database& db, const Key& key, const Identity& identity)
{
    const auto rows = db.exec(R"sql(
        SELECT to_char(expires_at AT TIME ZONE 'UTC','YYYY-MM-DD"T"HH24:MI:SS.US"Z"'),
            (SELECT coalesce(jsonb_agg(jsonb_build_object('site_id',site_id,'role',role) ORDER BY site_id),'[]')
             FROM bacuri_access.site_roles WHERE user_id=s.user_id AND enabled AND (s.oidc_site IS NULL OR site_id=s.oidc_site)),
            u.username
        FROM bacuri_access.browser_sessions s JOIN bacuri_access.local_users u ON u.user_id=s.user_id
        WHERE token_hash=$1
    )sql",
                              {fingerprint(key, "session", identity.cookie)});
    auto roles = Json::parse(rows.get(0, 1));
    const auto site = platform::optional_setting("BACURI_SITE_ID");
    const auto node = platform::optional_setting("BACURI_NODE_ID");
    if (site && node)
        for (auto& role : roles)
            if (role.at("site_id") == *site && role.at("role") == "administrator")
                role["node_id"] = *node;
    return {{"user_id", identity.user},
            {"username", rows.get(0, 2)},
            {"expires_at", rows.get(0)},
            {"site_roles", std::move(roles)},
            {"csrf_token", fingerprint(key, "csrf", identity.cookie)}};
}
} // namespace

void check_origin(const Request& request, const std::string& origin)
{
    if (request.count(http::field::origin) != 1 || request[http::field::origin] != origin) {
        throw Failure{ErrorCode::forbidden};
    }
}

bool Sessions::desktop_request(const Request& request)
{
    return desktop_bearer(request).has_value();
}

Sessions::Sessions(Key key, std::string origin)
    : key_{key}, origin_{std::move(origin)}, dummy_hash_{password_hash(token())}
{
}

Identity Sessions::authenticate(postgresql::QuerySession& db, const Request& request, bool mutation,
                                bool activity) const
{
    // A browser presents a cookie, a desktop client a bearer token. A request with both is refused.
    const auto desktop_token = desktop_bearer(request);
    auto value = cookie(request);
    if (desktop_token && !value.empty()) {
        throw Failure{ErrorCode::unauthenticated};
    }
    const bool desktop_client = desktop_token.has_value();
    if (desktop_client) {
        value = *desktop_token;
    }
    if (value.empty()) {
        throw Failure{ErrorCode::unauthenticated};
    }
    const auto rows =
        db.exec(activity ? "UPDATE bacuri_access.browser_sessions SET last_active=now() WHERE token_hash=$1 AND "
                           "client=$2 AND expires_at>now() "
                           "AND (oidc_until IS NULL OR oidc_until>clock_timestamp()) AND "
                           "last_active>now()-interval '30 minutes' RETURNING user_id,oidc_site"
                         : "SELECT user_id,oidc_site FROM bacuri_access.browser_sessions WHERE token_hash=$1 AND "
                           "client=$2 AND expires_at>now() "
                           "AND (oidc_until IS NULL OR oidc_until>clock_timestamp()) AND "
                           "last_active>now()-interval '30 minutes'",
                {fingerprint(key_, "session", value), desktop_client ? "desktop" : "browser"});
    if (!rows.size()) {
        throw Failure{ErrorCode::unauthenticated};
    }
    const std::string_view target{request.target()};
    constexpr std::string_view sites = "/api/v1/sites/";
    if (!rows.get(0, 1).empty() && target.starts_with(sites) && target.substr(sites.size(), 36) != rows.get(0, 1))
        throw Failure{ErrorCode::forbidden};
    // The bearer token is not an ambient credential of a browser, so it needs no origin or CSRF check.
    if (mutation && !desktop_client) {
        check_origin(request, origin_);
        if (request.count("X-CSRF-Token") != 1 || request["X-CSRF-Token"] != fingerprint(key_, "csrf", value)) {
            throw Failure{ErrorCode::forbidden};
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
    postgresql::Database db;
    const auto attempts = db.exec(R"sql(
            INSERT INTO bacuri_access.login_attempts VALUES ($1,now(),1) ON CONFLICT (client_address) DO UPDATE SET
            attempts=CASE WHEN login_attempts.started_at<now()-interval '1 minute' THEN 1 ELSE login_attempts.attempts+1 END,
            started_at=CASE WHEN login_attempts.started_at<now()-interval '1 minute' THEN now() ELSE login_attempts.started_at END
            RETURNING attempts
        )sql",
                                  {peer});
    if (std::stoi(attempts.get(0)) > 12) {
        response.set("Retry-After", "60");
        throw Failure{ErrorCode::rate_limited};
    }
    db.exec("BEGIN");
    auto user = db.exec("SELECT user_id,password_hash FROM bacuri_access.local_users WHERE username=$1 FOR UPDATE",
                        {username});
    if (!password_matches(user.size() ? user.get(0, 1) : dummy_hash_, password) || !user.size()) {
        throw Failure{ErrorCode::invalid_credentials};
    }
    if (!db.exec("SELECT FROM bacuri_access.site_roles WHERE user_id=$1 AND enabled", {user.get(0)}).size())
        throw Failure{ErrorCode::invalid_credentials};
    if (db.exec("SELECT FROM bacuri_access.site_roles WHERE user_id=$1 AND enabled AND role='administrator'",
                {user.get(0)})
            .size()) {
        auto challenge = begin_webauthn(db, key_, origin_, user.get(0),
                                        body.contains("enrollment_code")
                                            ? std::optional{string_field(body, "enrollment_code", 64)}
                                            : std::nullopt);
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
    postgresql::Database db;
    db.exec("BEGIN");
    const auto ticket = fingerprint(key_, "webauthn", string_field(body, "ticket", 64));
    db.exec("SAVEPOINT verify_factor");
    VerifiedFactor result;
    try {
        result = finish_webauthn(db, key_, origin_, body.get<FactorResponse>());
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
void Sessions::issue(postgresql::Database& db, const Request& request, Response& response,
                     const std::string& user_id, const std::string& site, const std::string& refresh) const
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
        postgresql::Database db;
        db.exec("BEGIN");
        auto identity = authenticate(db, request, false, false);
        respond(response, session_body(db, key_, identity));
        db.exec("COMMIT");
        return;
    }
    default:
        response.set(http::field::allow, "GET, POST, DELETE");
        throw Failure{ErrorCode::method_not_allowed};
    }
}
void Sessions::logout(const Request& request, Response& response) const
{
    const bool desktop_client = desktop_bearer(request).has_value();
    if (!desktop_client) {
        check_origin(request, origin_);
    }
    postgresql::Database db;
    db.exec("BEGIN");
    if (desktop_client || !cookie(request).empty()) {
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
            if (error.status() != 401) {
                throw;
            }
        }
    }
    db.exec("COMMIT");
    if (!desktop_client) {
        response.set("Set-Cookie", std::string{cookie_name} + "; Max-Age=0" + std::string{attributes});
    }
    response.result(http::status::no_content);
}

void Sessions::desktop_code(const Request& request, Response& response) const
{
    if (request.method() != http::verb::post) {
        response.set(http::field::allow, "POST");
        throw Failure{ErrorCode::method_not_allowed};
    }
    // Only a browser session issues codes, a desktop session cannot extend itself.
    if (request.count(http::field::authorization)) {
        throw Failure{ErrorCode::forbidden};
    }
    const auto body = object_body(request, {"code_challenge", "code_challenge_method", "redirect_uri"});
    const auto challenge = string_field(body, "code_challenge", 43);
    const auto redirect = string_field(body, "redirect_uri", 128);
    if (string_field(body, "code_challenge_method", 8) != "S256" || !desktop::valid_challenge(challenge) ||
        !desktop::loopback_redirect(redirect)) {
        throw Failure{ErrorCode::invalid_request};
    }
    postgresql::Database db;
    db.exec("BEGIN");
    const auto identity = authenticate(db, request, true);
    const auto code = token();
    db.exec("DELETE FROM bacuri_access.desktop_codes WHERE expires_at<=now()");
    const auto rows =
        db.exec("INSERT INTO bacuri_access.desktop_codes VALUES($1,$2,$3,$4,now()+interval '60 seconds') "
                "RETURNING to_char(expires_at AT TIME ZONE 'UTC','YYYY-MM-DD\"T\"HH24:MI:SS.US\"Z\"')",
                {fingerprint(key_, "desktop-code", code), fingerprint(key_, "session", identity.cookie), challenge,
                 redirect});
    db.exec("COMMIT");
    response.set(http::field::cache_control, "no-store");
    respond(response, {{"code", code}, {"expires_at", rows.get(0)}}, 201);
}

void Sessions::desktop_session(const Request& request, Response& response) const
{
    if (request.method() != http::verb::post) {
        response.set(http::field::allow, "POST");
        throw Failure{ErrorCode::method_not_allowed};
    }
    // The exchange carries no credentials. A page of another origin may not perform it.
    if (request.count(http::field::cookie) || request.count(http::field::authorization) ||
        (request.count(http::field::origin) && request[http::field::origin] != origin_)) {
        throw Failure{ErrorCode::forbidden};
    }
    const auto body = object_body(request, {"code", "code_verifier", "redirect_uri"});
    const auto code = string_field(body, "code", 64);
    const auto verifier = string_field(body, "code_verifier", 128);
    const auto redirect = string_field(body, "redirect_uri", 128);
    // A code is 256 random bits, valid once for 60 seconds, so the exchange needs no attempt limit of its own
    // and does not use up the password sign-in attempts of the address.
    postgresql::Database db;
    db.exec("BEGIN");
    // A code is consumed by the first exchange, whether it succeeds or not.
    const auto issued = db.exec("DELETE FROM bacuri_access.desktop_codes WHERE code_hash=$1 "
                                "RETURNING browser_hash,challenge,redirect_uri,expires_at>now()",
                                {fingerprint(key_, "desktop-code", code)});
    if (!issued.size() || issued.get(0, 3) != "t" || issued.get(0, 2) != redirect ||
        !desktop::verifier_matches(verifier, issued.get(0, 1))) {
        db.exec("COMMIT");
        throw Failure{ErrorCode::desktop_code_invalid};
    }
    // The browser session that issued the code must still be valid.
    const auto parent = db.exec("SELECT user_id,login_method,coalesce(oidc_site::text,''),"
                                "least(expires_at,now()+interval '8 hours') "
                                "FROM bacuri_access.browser_sessions WHERE token_hash=$1 AND "
                                "client='browser' AND expires_at>now() AND "
                                "(oidc_until IS NULL OR oidc_until>clock_timestamp()) AND "
                                "last_active>now()-interval '30 minutes'",
                                {issued.get(0)});
    if (!parent.size()) {
        db.exec("COMMIT");
        throw Failure{ErrorCode::desktop_code_invalid};
    }
    const Identity identity{parent.get(0), token()};
    db.exec(
        "INSERT INTO bacuri_access.browser_sessions(token_hash,user_id,expires_at,login_method,oidc_site,client) "
        "VALUES($1,$2,$3,$4,nullif($5,'')::uuid,'desktop')",
        {fingerprint(key_, "session", identity.cookie), identity.user, parent.get(0, 3), parent.get(0, 1),
         parent.get(0, 2)});
    db.exec("INSERT INTO bacuri_access.access_audit(site_id,actor,method,action,resource) SELECT "
            "site_id,$1::uuid::text,'desktop','session.login',$1::uuid::text "
            "FROM bacuri_access.site_roles WHERE user_id=$1::uuid AND enabled AND ($2='' OR "
            "site_id=nullif($2,'')::uuid)",
            {identity.user, parent.get(0, 2)});
    auto result = session_body(db, key_, identity);
    result.erase("csrf_token");
    result["token"] = std::string{desktop::token_prefix} + identity.cookie;
    db.exec("COMMIT");
    response.set(http::field::cache_control, "no-store");
    respond(response, result, 201);
}
} // namespace bacuri::vms::control
