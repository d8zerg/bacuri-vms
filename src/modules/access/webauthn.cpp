#include "application/control_api/http_json.hpp"
#include "modules/access/webauthn.hpp"
#include <fido.h>
#include <fido/es256.h>
#include <sodium.h>

namespace bacuri::vms::control
{
namespace
{
constexpr int encoding = sodium_base64_VARIANT_URLSAFE_NO_PADDING;
using Bytes = std::vector<unsigned char>;
void require(bool ok)
{
    if (!ok)
        throw Failure{ErrorCode::invalid_factor};
}
std::string encode(const unsigned char* p, std::size_t n)
{
    std::string s(sodium_base64_ENCODED_LEN(n, encoding), '\0');
    sodium_bin2base64(s.data(), s.size(), p, n, encoding);
    s.resize(std::char_traits<char>::length(s.c_str()));
    return s;
}
Bytes decode(const std::string& s)
{
    Bytes b(s.size());
    std::size_t n{};
    require(!s.empty() &&
            sodium_base642bin(b.data(), b.size(), s.data(), s.size(), nullptr, &n, nullptr, encoding) == 0);
    b.resize(n);
    return b;
}
std::string rp_id(const std::string& origin)
{
    std::unique_ptr<GUri, decltype(&g_uri_unref)> uri{g_uri_parse(origin.c_str(), G_URI_FLAGS_NONE, nullptr),
                                                      g_uri_unref};
    if (!uri || !g_uri_get_host(uri.get()))
        throw std::invalid_argument("Invalid WebAuthn origin");
    return g_uri_get_host(uri.get());
}
template <class T, void (*Free)(T**)> struct FreeFido {
    void operator()(T* p) const
    {
        Free(&p);
    }
};
void check(int result)
{
    require(result == FIDO_OK);
}
} // namespace
std::string enrollment_code(const std::string& username, Enrollment mode)
{
    postgresql::Database db;
    db.exec("BEGIN");
    auto row = db.exec("SELECT u.user_id FROM bacuri_access.local_users u WHERE username=$1 AND EXISTS(SELECT "
                       "FROM bacuri_access.site_roles r "
                       "WHERE r.user_id=u.user_id AND role='administrator') FOR UPDATE",
                       {username});
    if (!row.size())
        throw std::invalid_argument("Expected a local administrator");
    if (mode == Enrollment::recovery) {
        db.exec("DELETE FROM bacuri_access.webauthn_credentials WHERE user_id=$1", {row.get(0)});
        db.exec("DELETE FROM bacuri_access.browser_sessions WHERE user_id=$1", {row.get(0)});
        db.exec("INSERT INTO bacuri_access.credential_audit(user_id,action) VALUES($1,'recovery_started')",
                {row.get(0)});
    } else if (db.exec("SELECT FROM bacuri_access.webauthn_credentials WHERE user_id=$1", {row.get(0)}).size())
        throw std::invalid_argument("Expected administrator awaiting first enrollment");
    const auto secret = token();
    db.exec("DELETE FROM bacuri_access.webauthn_challenges WHERE user_id=$1", {row.get(0)});
    db.exec("INSERT INTO bacuri_access.webauthn_enrollment VALUES($1,$2,now()+interval '10 minutes') ON "
            "CONFLICT(user_id) DO "
            "UPDATE SET code_hash=excluded.code_hash,expires_at=excluded.expires_at",
            {row.get(0), fingerprint(load_key(setting("BACURI_KEY_FILE")), "enroll", secret)});
    db.exec("COMMIT");
    return secret;
}
FactorChallenge begin_webauthn(postgresql::Database& db, const Key& key, const std::string& origin,
                               const std::string& user, const std::optional<std::string>& enrollment_code)
{
    auto cred = db.exec("SELECT credential_id FROM bacuri_access.webauthn_credentials WHERE user_id=$1", {user});
    if (!cred.size()) {
        if (!enrollment_code)
            throw Failure{ErrorCode::enrollment_required};
        const auto& secret = *enrollment_code;
        require(db.exec("SELECT FROM bacuri_access.webauthn_enrollment WHERE user_id=$1 AND code_hash=$2 AND "
                        "expires_at>clock_timestamp()",
                        {user, fingerprint(key, "enroll", secret)})
                    .size());
    }
    const auto ticket = token();
    std::array<unsigned char, 32> nonce{};
    randombytes_buf(nonce.data(), nonce.size());
    const auto challenge = encode(nonce.data(), nonce.size());
    db.exec("DELETE FROM bacuri_access.webauthn_challenges WHERE expires_at<=now()");
    db.exec("INSERT INTO bacuri_access.webauthn_challenges(challenge,ticket_hash,user_id,enrolling,expires_at) "
            "VALUES($1,$2,$3,$4,now()+interval '2 minutes')",
            {challenge, fingerprint(key, "webauthn", ticket), user, cred.size() ? "false" : "true"});
    return {challenge,
            ticket,
            rp_id(origin),
            user,
            cred.size() ? "get" : "create",
            cred.size() ? std::optional{cred.get(0)} : std::nullopt};
}
VerifiedFactor finish_webauthn(postgresql::Database& db, const Key& key, const std::string& origin,
                               const FactorResponse& body)
{
    const auto ticket = fingerprint(key, "webauthn", body.ticket);
    auto pending = db.exec("SELECT u.user_id FROM bacuri_access.local_users u JOIN "
                           "bacuri_access.webauthn_challenges c USING(user_id) WHERE "
                           "c.ticket_hash=$1 FOR UPDATE OF u",
                           {ticket});
    require(pending.size());
    auto challenge = db.exec("DELETE FROM bacuri_access.webauthn_challenges WHERE ticket_hash=$1 AND "
                             "expires_at>clock_timestamp() RETURNING challenge,user_id,enrolling",
                             {ticket});
    require(challenge.size());
    const bool enroll = challenge.get(0, 2) == "t";
    const auto user = challenge.get(0, 1), rp = rp_id(origin);
    const auto client = decode(body.client_data);
    const auto parsed = Json::parse(client, nullptr, false);
    require(parsed.is_object() && parsed.contains("type") && parsed.contains("origin") &&
            parsed.contains("challenge") && parsed["type"] == (enroll ? "webauthn.create" : "webauthn.get") &&
            parsed["origin"] == origin && parsed["challenge"] == challenge.get(0) &&
            (!parsed.contains("crossOrigin") || parsed["crossOrigin"] == false) && !parsed.contains("topOrigin"));
    const auto data = decode(body.authenticator_data);
    std::array<unsigned char, crypto_hash_sha256_BYTES> hash{};
    crypto_hash_sha256(hash.data(), reinterpret_cast<const unsigned char*>(rp.data()), rp.size());
    require(data.size() >= 37 && sodium_memcmp(hash.data(), data.data(), hash.size()) == 0 && (data[32] & 5) == 5);
    const auto id = body.credential_id;
    if (enroll) {
        require((data[32] & 0x40) != 0 && !body.signature);
        require(db.exec("DELETE FROM bacuri_access.webauthn_enrollment WHERE user_id=$1 AND "
                        "expires_at>clock_timestamp() "
                        "RETURNING user_id",
                        {user})
                    .size());
        std::unique_ptr<fido_cred_t, FreeFido<fido_cred_t, fido_cred_free>> cred(fido_cred_new());
        check(fido_cred_set_type(cred.get(), COSE_ES256));
        check(fido_cred_set_authdata_raw(cred.get(), data.data(), data.size()));
        require(fido_cred_pubkey_len(cred.get()) == 64 && fido_cred_id_len(cred.get()) > 0 &&
                id == encode(fido_cred_id_ptr(cred.get()), fido_cred_id_len(cred.get())));
        db.exec("INSERT INTO bacuri_access.webauthn_credentials VALUES($1,$2,$3,$4)",
                {user, id, encode(fido_cred_pubkey_ptr(cred.get()), 64),
                 std::to_string(fido_cred_sigcount(cred.get()))});
        db.exec("DELETE FROM bacuri_access.webauthn_challenges WHERE user_id=$1", {user});
    } else {
        auto row = db.exec("SELECT public_key,counter FROM bacuri_access.webauthn_credentials WHERE user_id=$1 "
                           "AND credential_id=$2 FOR UPDATE",
                           {user, id});
        require(row.size() && !(data[32] & 0x40));
        if (!body.signature)
            throw Failure{ErrorCode::invalid_request};
        const auto pub = decode(row.get(0)), sig = decode(*body.signature);
        std::unique_ptr<es256_pk_t, FreeFido<es256_pk_t, es256_pk_free>> pk(es256_pk_new());
        check(es256_pk_from_ptr(pk.get(), pub.data(), pub.size()));
        std::unique_ptr<fido_assert_t, FreeFido<fido_assert_t, fido_assert_free>> assertion(fido_assert_new());
        check(fido_assert_set_rp(assertion.get(), rp.c_str()));
        check(fido_assert_set_clientdata(assertion.get(), client.data(), client.size()));
        check(fido_assert_set_count(assertion.get(), 1));
        check(fido_assert_set_authdata_raw(assertion.get(), 0, data.data(), data.size()));
        check(fido_assert_set_sig(assertion.get(), 0, sig.data(), sig.size()));
        check(fido_assert_set_up(assertion.get(), FIDO_OPT_TRUE));
        check(fido_assert_set_uv(assertion.get(), FIDO_OPT_TRUE));
        check(fido_assert_verify(assertion.get(), 0, COSE_ES256, pk.get()));
        const auto count = fido_assert_sigcount(assertion.get(), 0);
        const auto previous = std::stoul(row.get(0, 1));
        require((!previous && !count) || count > previous);
        db.exec("UPDATE bacuri_access.webauthn_credentials SET counter=$2 WHERE user_id=$1",
                {user, std::to_string(count)});
    }
    return {user, enroll};
}
} // namespace bacuri::vms::control
