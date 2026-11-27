#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace bacuri::vms::control::desktop
{
// Prefix of a desktop session token in the Authorization header: "Bearer d1.<64 hex digits>".
inline constexpr std::string_view token_prefix = "d1.";

// The token of a desktop session from an Authorization header value, or nothing when the
// header names another credential, such as an alert delegation.
std::optional<std::string> session_token(std::string_view authorization);

// A PKCE S256 challenge: 43 characters of unpadded base64url.
bool valid_challenge(std::string_view challenge);

// A PKCE verifier of 43 to 128 unreserved characters whose SHA-256 in unpadded base64url is the challenge.
bool verifier_matches(std::string_view verifier, std::string_view challenge);

// A loopback redirect of a native application: http on 127.0.0.1 or [::1], an explicit port from
// 1024 to 65535 and a short path without a query or a fragment.
bool loopback_redirect(std::string_view uri);
} // namespace bacuri::vms::control::desktop
