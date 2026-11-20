#include "control/api.hpp"

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
             FROM site_roles WHERE user_id=s.user_id)
        FROM browser_sessions s WHERE token_hash=$1
    )sql",
                              {fingerprint(key, "session", identity.cookie)});
    return {{"user_id", identity.user},
            {"expires_at", rows.get(0)},
            {"site_roles", Json::parse(rows.get(0, 1))},
            {"csrf_token", fingerprint(key, "csrf", identity.cookie)}};
}
} // namespace

void respond(Response& response, const Json& body, int status)
{
    response.result(status);
    response.set(http::field::content_type, "application/json");
    response.body() = body.dump();
}

Json object_body(const Request& request, std::initializer_list<std::string_view> fields)
{
    const auto mime = request[http::field::content_type];
    if (mime.substr(0, mime.find(';')) != "application/json") {
        throw Failure{400, "invalid_request"};
    }
    auto object = Json::parse(request.body(), nullptr, false);
    if (!object.is_object()) {
        throw Failure{400, "invalid_request"};
    }
    for (const auto& [field, value] : object.items()) {
        if (std::ranges::find(fields, field) == fields.end()) {
            throw Failure{400, "invalid_request"};
        }
    }
    return object;
}

std::string string_field(const Json& object, std::string_view field, std::size_t limit, bool allow_empty)
{
    auto it = object.find(field);
    if (it == object.end() || !it->is_string()) {
        throw Failure{400, "invalid_request"};
    }
    const auto& value = it->get_ref<const std::string&>();
    const auto characters = std::ranges::count_if(value, [](unsigned char ch) { return (ch & 0xc0) != 0x80; });
    if ((!allow_empty && value.empty()) || static_cast<std::size_t>(characters) > limit || value.contains('\0')) {
        throw Failure{400, "invalid_request"};
    }
    return value;
}

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
    const auto rows = db.exec(
        activity ? "UPDATE browser_sessions SET last_active=now() WHERE token_hash=$1 AND expires_at>now() "
                   "AND last_active>now()-interval '30 minutes' RETURNING user_id"
                 : "SELECT user_id FROM browser_sessions WHERE token_hash=$1 AND expires_at>now() "
                   "AND last_active>now()-interval '30 minutes'",
        {fingerprint(key_, "session", value)});
    if (!rows.size()) {
        throw Failure{401, "unauthenticated"};
    }
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
    const auto body = object_body(request, {"username", "password"});
    const auto username = string_field(body, "username", 128);
    const auto password = string_field(body, "password", 1024);
    Database db;
    const auto attempts = db.exec(R"sql(
            INSERT INTO login_attempts VALUES ($1,now(),1) ON CONFLICT (client_address) DO UPDATE SET
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
    auto user = db.exec("SELECT user_id,password_hash FROM local_users WHERE username=$1 FOR SHARE", {username});
    if (!password_matches(user.size() ? user.get(0, 1) : dummy_hash_, password) || !user.size()) {
        throw Failure{401, "invalid_credentials"};
    }
    auto previous = cookie(request);
    db.exec("DELETE FROM browser_sessions WHERE token_hash=$1 OR expires_at<=now() OR "
            "last_active<=now()-interval '30 minutes'",
            {fingerprint(key_, "session", previous)});
    const Identity identity{user.get(0), token()};
    db.exec("INSERT INTO browser_sessions(token_hash,user_id,expires_at) VALUES ($1,$2,now()+interval '8 hours')",
            {fingerprint(key_, "session", identity.cookie), identity.user});
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
            db.exec("DELETE FROM browser_sessions WHERE token_hash=$1",
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
