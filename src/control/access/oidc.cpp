#include "control/access/oidc.hpp"
#include "control/access/operations.hpp"
#include <curl/curl.h>
#include <sodium.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace bacuri::vms::control
{
namespace
{
using namespace std::chrono_literals;
void valid(bool condition)
{
    if (!condition)
        throw Failure{ErrorCode::invalid_identity};
}
bool issuer_url(const std::string& s)
{
    std::unique_ptr<GUri, decltype(&g_uri_unref)> uri{g_uri_parse(s.c_str(), G_URI_FLAGS_NONE, nullptr),
                                                      g_uri_unref};
    return uri && s.starts_with("https://") && g_uri_get_host(uri.get()) && *g_uri_get_host(uri.get()) &&
           !g_uri_get_userinfo(uri.get()) && !g_uri_get_query(uri.get()) && !g_uri_get_fragment(uri.get()) &&
           !s.ends_with('/') && s.find_first_of(" \r\n\t\\%") == s.npos;
}
std::string encode(std::string_view input)
{
    std::unique_ptr<gchar, decltype(&g_free)> out{g_uri_escape_string(std::string(input).c_str(), nullptr, false),
                                                  g_free};
    return out.get();
}
std::string base64(std::span<const unsigned char> data)
{
    auto size = sodium_base64_ENCODED_LEN(data.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    std::string out(size, '\0');
    sodium_bin2base64(out.data(), size, data.data(), data.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    out.resize(strlen(out.c_str()));
    return out;
}
std::vector<unsigned char> decode(std::string_view input, int variant = sodium_base64_VARIANT_URLSAFE_NO_PADDING)
{
    std::vector<unsigned char> out(input.size());
    size_t size{};
    valid(!input.empty() && sodium_base642bin(out.data(), out.size(), input.data(), input.size(), nullptr, &size,
                                              nullptr, variant) == 0);
    out.resize(size);
    return out;
}
std::string challenge(const std::string& verifier)
{
    std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
    crypto_hash_sha256(digest.data(), reinterpret_cast<const unsigned char*>(verifier.data()), verifier.size());
    return base64(digest);
}
size_t collect(char* data, size_t size, size_t count, void* context)
{
    auto& body = *static_cast<std::string*>(context);
    constexpr size_t limit = 512uz * 1024;
    if (body.size() > limit || (size && count > (limit - body.size()) / size))
        return 0;
    const auto n = size * count;
    try {
        body.append(data, n);
    } catch (...) {
        return 0;
    }
    return n;
}
Json fetch(const std::string& url, const std::string& fields = {})
{
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl{curl_easy_init(), curl_easy_cleanup};
    if (!curl)
        throw Failure{ErrorCode::identity_unavailable};
    auto* c = curl.get();
    std::string body;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 2500L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
    const auto ca = optional_setting("BACURI_OIDC_CA_FILE");
    if (ca)
        curl_easy_setopt(c, CURLOPT_CAINFO, ca->c_str());
    if (!fields.empty())
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, fields.c_str());
    if (curl_easy_perform(c) != CURLE_OK)
        throw Failure{ErrorCode::identity_unavailable};
    long status{};
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (status == 400 || status == 401 || status == 403)
        throw Failure{ErrorCode::invalid_identity};
    if (status != 200)
        throw Failure{ErrorCode::identity_unavailable};
    auto result = Json::parse(body, nullptr, false);
    valid(result.is_object());
    return result;
}
struct Provider {
    std::string issuer, client, secret, acr;
    std::string endpoint(std::string_view name) const
    {
        return issuer + "/protocol/openid-connect/" + std::string{name};
    }
    std::string credentials() const
    {
        return "&client_id=" + encode(client) + "&client_secret=" + encode(secret);
    }
};
Provider provider(Database& db, const std::string& site, const Key& key)
{
    auto r = db.exec("SELECT issuer,client_id,sealed_secret,admin_acr FROM bacuri_access.identity_providers WHERE "
                     "site_id=$1 AND enabled FOR SHARE",
                     {site});
    if (!r.size())
        throw Failure{ErrorCode::identity_unavailable};
    return {r.get(0), r.get(0, 1), unseal(r.get(0, 2), key, "oidc-provider/" + site), r.get(0, 3)};
}
Json claims(const Provider& config, const std::string& jwt)
{
    const auto a = jwt.find('.'), b = jwt.rfind('.');
    valid(a != jwt.npos && a < b && jwt.find('.', a + 1) == b);
    auto parse = [](auto segment) {
        auto bytes = decode(segment);
        auto j = Json::parse(bytes, nullptr, false);
        valid(j.is_object());
        return j;
    };
    const std::string_view text{jwt};
    auto header = parse(text.substr(0, a));
    valid(header.value("alg", "") == "RS256" && !header.contains("crit") && header.contains("kid"));
    auto keys = fetch(config.endpoint("certs"));
    bool verified = false;
    auto signature = decode(text.substr(b + 1));
    for (const auto& jwk : keys.at("keys")) {
        if (jwk.value("kid", "") != header.value("kid", "") || jwk.value("kty", "") != "RSA" ||
            jwk.value("use", "sig") != "sig" || jwk.value("alg", "RS256") != "RS256" || !jwk.contains("x5c") ||
            jwk.at("x5c").empty())
            continue;
        auto der = decode(jwk.at("x5c").at(0).get<std::string>(), sodium_base64_VARIANT_ORIGINAL);
        const unsigned char* ptr = der.data();
        std::unique_ptr<X509, decltype(&X509_free)> cert{d2i_X509(nullptr, &ptr, static_cast<long>(der.size())),
                                                         X509_free};
        valid(bool(cert));
        auto* pub = X509_get0_pubkey(cert.get());
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> md{EVP_MD_CTX_new(), EVP_MD_CTX_free};
        valid(pub && EVP_PKEY_is_a(pub, "RSA") && EVP_PKEY_get_bits(pub) >= 2048 && md &&
              EVP_DigestVerifyInit(md.get(), nullptr, EVP_sha256(), nullptr, pub) == 1);
        verified = EVP_DigestVerify(md.get(), signature.data(), signature.size(),
                                    reinterpret_cast<const unsigned char*>(jwt.data()), b) == 1;
        break;
    }
    valid(verified);
    auto result = parse(text.substr(a + 1, b - a - 1));
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto aud = result.at("aud");
    valid((aud == config.client || (aud.is_array() && std::ranges::find(aud, Json(config.client)) != aud.end())) &&
          (aud.size() <= 1 || result.contains("azp")) && result.value("azp", config.client) == config.client &&
          result.value("iss", "") == config.issuer && result.at("exp").get<long long>() > now &&
          result.at("iat").get<long long>() <= now + 5 && result.value("nbf", 0LL) <= now + 5 &&
          result.at("sub").is_string() && !result.at("sub").get_ref<const std::string&>().empty());
    return result;
}
std::string flow_cookie(const Request& req)
{
    auto value = std::string{req[http::field::cookie]};
    std::string_view s{value};
    while (!s.empty()) {
        auto next = s.find(';');
        auto item = s.substr(0, next);
        auto pos = item.find_first_not_of(' ');
        if (pos != item.npos && item.substr(pos).starts_with("__Host-bacuri_oidc="))
            return std::string{item.substr(pos + 19)};
        if (next == s.npos)
            break;
        s.remove_prefix(next + 1);
    }
    return {};
}
} // namespace
namespace access
{
Json oidc_provider(Database& db, const Request& req, const Key& key, const std::string& site)
{
    if (req.method() == http::verb::get) {
        auto rows = db.exec(
            "SELECT "
            "jsonb_build_object('enabled',enabled,'issuer',issuer,'client_id',client_id,'admin_acr',admin_acr) "
            "FROM bacuri_access.identity_providers WHERE site_id=$1",
            {site});
        return rows.size() ? Json::parse(rows.get(0)) : Json{{"enabled", false}};
    }
    if (req.method() != http::verb::put)
        throw Failure{ErrorCode::method_not_allowed};
    return configure_provider(db, key, site,
                              object_body(req, {"enabled", "issuer", "client_id", "client_secret", "admin_acr"}));
}
Json configure_provider(Database& db, const Key& key, const std::string& site, const Json& body)
{
    auto issuer = string_field(body, "issuer", 1024), client = string_field(body, "client_id", 128),
         acr = string_field(body, "admin_acr", 128);
    if (!issuer_url(issuer) || !body.contains("enabled") || !body["enabled"].is_boolean() || acr == "0" ||
        acr == "1")
        throw Failure{ErrorCode::invalid_request};
    std::string secret;
    if (body.contains("client_secret"))
        secret = seal(key, "oidc-provider/" + site, string_field(body, "client_secret", 2048));
    else {
        auto old = db.exec("SELECT sealed_secret FROM bacuri_access.identity_providers WHERE site_id=$1 AND "
                           "issuer=$2 AND client_id=$3",
                           {site, issuer, client});
        if (!old.size())
            throw Failure{ErrorCode::invalid_request};
        secret = old.get(0);
    }
    db.exec("INSERT INTO bacuri_access.identity_providers VALUES ($1,$2,$3,$4,$5,$6) ON CONFLICT(site_id) DO "
            "UPDATE SET "
            "enabled=excluded.enabled,issuer=excluded.issuer,client_id=excluded.client_id,sealed_secret=excluded."
            "sealed_secret,admin_acr=excluded.admin_acr",
            {site, body["enabled"] == true ? "true" : "false", issuer, client, secret, acr});
    db.exec("DELETE FROM bacuri_access.browser_sessions WHERE oidc_site=$1", {site});
    db.exec("DELETE FROM bacuri_access.oidc_requests WHERE site_id=$1", {site});
    return {{"id", site}};
}
Json external_identity(Database& db, const Request& req, const std::string& site, const std::string& user)
{
    if (req.method() != http::verb::delete_ && req.method() != http::verb::put)
        throw Failure{ErrorCode::method_not_allowed};
    return bind_identity(db, site, user,
                         req.method() == http::verb::delete_ ? Json(nullptr)
                                                             : object_body(req, {"issuer", "subject"}));
}
Json bind_identity(Database& db, const std::string& site, const std::string& user, const Json& body)
{
    if (!canonical_id(user) ||
        !db.exec("SELECT FROM bacuri_access.site_roles WHERE site_id=$1 AND user_id=$2 FOR UPDATE", {site, user})
             .size())
        throw Failure{ErrorCode::not_found};
    if (body.is_null())
        db.exec("DELETE FROM bacuri_access.external_identities WHERE site_id=$1 AND user_id=$2", {site, user});
    else {
        auto issuer = string_field(body, "issuer", 1024), subject = string_field(body, "subject", 255);
        if (!db.exec("SELECT FROM bacuri_access.identity_providers WHERE site_id=$1 AND issuer=$2", {site, issuer})
                 .size())
            throw Failure{ErrorCode::invalid_request};
        if (db.exec("SELECT FROM bacuri_access.external_identities WHERE issuer=$1 AND subject=$2 AND "
                    "(site_id,user_id)<>($3,$4)",
                    {issuer, subject, site, user})
                .size())
            throw Failure{ErrorCode::identity_conflict};
        db.exec("INSERT INTO bacuri_access.external_identities VALUES ($1,$2,$3,$4) ON CONFLICT(site_id,user_id) "
                "DO UPDATE SET "
                "issuer=excluded.issuer,subject=excluded.subject",
                {site, user, issuer, subject});
    }
    db.exec("DELETE FROM bacuri_access.browser_sessions WHERE oidc_site=$1 AND user_id=$2", {site, user});
    return {{"id", user}};
}
} // namespace access

void Sessions::corporate(const Request& req, Response& res, std::string_view path) const
{
    allow(req, res, {http::verb::get});
    const auto site = setting("BACURI_SITE_ID");
    Database db;
    if (path == "methods") {
        return respond(
            res,
            {{"local", true},
             {"oidc",
              db.exec("SELECT FROM bacuri_access.identity_providers WHERE site_id=$1 AND enabled", {site}).size() >
                  0}});
    }
    db.exec("BEGIN");
    auto config = provider(db, site, key_);
    const auto redirect = origin_ + "/api/v1/session/oidc/callback";
    if (path == "start") {
        auto metadata = fetch(config.issuer + "/.well-known/openid-configuration");
        if (metadata.value("issuer", "") != config.issuer ||
            metadata.value("authorization_endpoint", "") != config.endpoint("auth"))
            throw Failure{ErrorCode::identity_unavailable};
        const auto state = token(), browser = token(), verifier = token(), nonce = token();
        db.exec("DELETE FROM bacuri_access.oidc_requests WHERE expires_at<=now()");
        db.exec("INSERT INTO bacuri_access.oidc_requests VALUES($1,$2,$3,$4,$5,now()+interval '5 minutes')",
                {fingerprint(key_, "oidc-state", state), fingerprint(key_, "oidc-browser", browser), verifier,
                 nonce, site});
        db.exec("COMMIT");
        res.result(http::status::see_other);
        res.set("Set-Cookie",
                "__Host-bacuri_oidc=" + browser + "; Path=/; Max-Age=300; Secure; HttpOnly; SameSite=Lax");
        res.set(http::field::location,
                config.endpoint("auth") + "?response_type=code&scope=openid&client_id=" + encode(config.client) +
                    "&redirect_uri=" + encode(redirect) + "&state=" + state + "&nonce=" + nonce +
                    "&code_challenge_method=S256&code_challenge=" + challenge(verifier) +
                    "&acr_values=" + encode(config.acr));
        return;
    }
    if (path != "callback")
        throw Failure{ErrorCode::not_found};
    const auto target = std::string{req.target()};
    auto pos = target.find('?');
    valid(pos != target.npos);
    std::unique_ptr<GHashTable, decltype(&g_hash_table_unref)> params{
        g_uri_parse_params(target.data() + pos + 1, -1, "&", G_URI_PARAMS_NONE, nullptr), g_hash_table_unref};
    valid(bool(params));
    auto param = [&](const char* name) {
        auto* val = static_cast<const char*>(g_hash_table_lookup(params.get(), name));
        valid(val && *val);
        return std::string{val};
    };
    const auto state = param("state"), code = param("code");
    auto flow = db.exec(
        "DELETE FROM bacuri_access.oidc_requests WHERE state_hash=$1 AND browser_hash=$2 AND site_id=$3 AND "
        "expires_at>now() RETURNING verifier,nonce",
        {fingerprint(key_, "oidc-state", state), fingerprint(key_, "oidc-browser", flow_cookie(req)), site});
    valid(flow.size());
    db.exec("COMMIT");
    db.exec("BEGIN");
    auto current = provider(db, site, key_);
    valid(current.issuer == config.issuer && current.client == config.client && current.secret == config.secret);
    auto response = fetch(current.endpoint("token"), "grant_type=authorization_code&code=" + encode(code) +
                                                         "&redirect_uri=" + encode(redirect) + "&code_verifier=" +
                                                         encode(flow.get(0)) + current.credentials());
    auto id = claims(current, response.at("id_token").get<std::string>());
    valid(id.value("nonce", "") == flow.get(0, 1));
    auto binding = db.exec("SELECT e.user_id,r.role FROM bacuri_access.external_identities e JOIN "
                           "bacuri_access.site_roles r USING(site_id,user_id) "
                           "WHERE e.site_id=$1 AND issuer=$2 AND subject=$3 AND r.enabled FOR SHARE OF e,r",
                           {site, current.issuer, id.at("sub").get<std::string>()});
    valid(binding.size() && (binding.get(0, 1) != "administrator" || id.value("acr", "") == current.acr));
    issue(db, req, res, binding.get(0), site, response.at("refresh_token").get<std::string>());
    res.result(http::status::see_other);
    res.body().clear();
    res.insert(http::field::set_cookie, "__Host-bacuri_oidc=; Path=/; Max-Age=0; Secure; HttpOnly; SameSite=Lax");
    res.set(http::field::location, "/monitor");
}

namespace access
{
void refresh_identities(const std::stop_token& stop, const Key& key)
{
    while (!stop.stop_requested()) {
        bool wait_before_retry = false;
        try {
            Database db;
            db.exec("BEGIN");
            const auto site = setting("BACURI_SITE_ID");
            lock_policy(db, site, true);
            auto rows = db.exec(R"sql(SELECT b.token_hash,b.oidc_site,b.sealed_refresh_token,e.subject,r.role
                FROM bacuri_access.browser_sessions b JOIN bacuri_access.external_identities e ON (e.user_id,e.site_id)=(b.user_id,b.oidc_site)
                JOIN bacuri_access.site_roles r ON (r.user_id,r.site_id)=(b.user_id,b.oidc_site)
                WHERE b.oidc_site=$1 AND b.oidc_until BETWEEN clock_timestamp() AND clock_timestamp()+interval '25 seconds'
                AND b.expires_at>now() AND b.last_active>now()-interval '30 minutes' AND r.enabled ORDER BY b.oidc_until
                LIMIT 1 FOR UPDATE OF b SKIP LOCKED)sql",
                                {site});
            if (rows.size()) {
                auto hash = rows.get(0);
                auto config = provider(db, rows.get(0, 1), key);
                try {
                    auto refreshed = fetch(config.endpoint("token"),
                                           "grant_type=refresh_token&refresh_token=" +
                                               encode(unseal(rows.get(0, 2), key, "oidc-session/" + hash)) +
                                               config.credentials());
                    auto id = claims(config, refreshed.at("id_token").get<std::string>());
                    valid(id.at("sub") == rows.get(0, 3) &&
                          (rows.get(0, 4) != "administrator" || id.value("acr", "") == config.acr));
                    db.exec(
                        "UPDATE bacuri_access.browser_sessions SET oidc_until=now()+interval '55 "
                        "seconds',sealed_refresh_token=$2 WHERE token_hash=$1 AND oidc_until>clock_timestamp()",
                        {hash,
                         seal(key, "oidc-session/" + hash, refreshed.at("refresh_token").get<std::string>())});
                } catch (const Failure& f) {
                    if (f.status() == 401)
                        db.exec("DELETE FROM bacuri_access.browser_sessions WHERE token_hash=$1", {hash});
                    else
                        throw;
                }
            }
            db.exec("COMMIT");
            wait_before_retry = rows.size() == 0;
        } catch (...) {
            // Expired identity checks deny subsequent access while the provider is unavailable.
            wait_before_retry = true;
        }
        if (wait_before_retry)
            std::this_thread::sleep_for(500ms);
    }
}
} // namespace access
} // namespace bacuri::vms::control
