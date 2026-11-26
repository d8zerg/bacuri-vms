#pragma once
#include <optional>
#include "modules/access/session.hpp"
#include <stop_token>
namespace bacuri::vms::control::access
{
struct IdentityProvider {
    bool enabled{};
    std::string issuer, client_id, admin_acr;
};
struct ProviderSettings {
    IdentityProvider provider;
    std::optional<std::string> client_secret;
};
struct ExternalIdentity {
    std::string issuer, subject;
};
std::optional<IdentityProvider> oidc_provider(postgresql::QuerySession&, const std::string& site);
void configure_provider(postgresql::QuerySession&, const Key&, const std::string& site, const ProviderSettings&);
void bind_identity(postgresql::QuerySession&, const std::string& site, const std::string& user,
                   const std::optional<ExternalIdentity>&);
void refresh_identities(const std::stop_token&, const Key&);
} // namespace bacuri::vms::control::access
