#include "modules/access/desktop.hpp"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <charconv>

namespace bacuri::vms::control::desktop
{
namespace
{
bool base64url(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

bool unreserved(char c)
{
    return base64url(c) || c == '.' || c == '~';
}

bool hex(std::string_view value)
{
    return value.size() == 64 &&
           std::ranges::all_of(value, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
} // namespace

std::optional<std::string> session_token(std::string_view authorization)
{
    constexpr std::string_view bearer = "Bearer ";
    if (!authorization.starts_with(bearer))
        return std::nullopt;
    authorization.remove_prefix(bearer.size());
    if (!authorization.starts_with(token_prefix))
        return std::nullopt;
    authorization.remove_prefix(token_prefix.size());
    if (!hex(authorization))
        return std::nullopt;
    return std::string{authorization};
}

bool valid_challenge(std::string_view challenge)
{
    return challenge.size() == 43 && std::ranges::all_of(challenge, base64url);
}

bool verifier_matches(std::string_view verifier, std::string_view challenge)
{
    if (verifier.size() < 43 || verifier.size() > 128 || !std::ranges::all_of(verifier, unreserved) ||
        !valid_challenge(challenge))
        return false;
    std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
    crypto_hash_sha256(digest.data(), reinterpret_cast<const unsigned char*>(verifier.data()), verifier.size());
    std::array<char, sodium_base64_ENCODED_LEN(crypto_hash_sha256_BYTES, sodium_base64_VARIANT_URLSAFE_NO_PADDING)>
        encoded{};
    sodium_bin2base64(encoded.data(), encoded.size(), digest.data(), digest.size(),
                      sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    return std::string_view{encoded.data()}.size() == challenge.size() &&
           sodium_memcmp(encoded.data(), challenge.data(), challenge.size()) == 0;
}

bool loopback_redirect(std::string_view uri)
{
    if (uri.size() > 128)
        return false;
    for (const std::string_view host : {"http://127.0.0.1:", "http://[::1]:"}) {
        if (!uri.starts_with(host))
            continue;
        auto rest = uri.substr(host.size());
        const auto slash = rest.find('/');
        if (slash == rest.npos)
            return false;
        const auto port_text = rest.substr(0, slash);
        unsigned port{};
        const auto [end, error] = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
        if (error != std::errc{} || end != port_text.data() + port_text.size() || port_text.starts_with('0') ||
            port < 1024 || port > 65535)
            return false;
        const auto path = rest.substr(slash);
        return std::ranges::all_of(path, [](char c) { return base64url(c) || c == '/'; });
    }
    return false;
}
} // namespace bacuri::vms::control::desktop
