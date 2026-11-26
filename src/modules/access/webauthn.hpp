#pragma once
#include <optional>
#include "modules/access/enrollment.hpp"
#include "application/control_api/api.hpp"

namespace bacuri::vms::control
{
struct FactorChallenge {
    std::string challenge, ticket, rp_id, user_id, kind;
    std::optional<std::string> credential_id;
};
struct FactorResponse {
    std::string ticket, client_data, authenticator_data, credential_id;
    std::optional<std::string> signature;
};
FactorChallenge begin_webauthn(postgresql::Database&, const Key&, const std::string& origin,
                               const std::string& user, const std::optional<std::string>& enrollment_code);
struct VerifiedFactor {
    std::string user;
    bool enrolled{};
};
VerifiedFactor finish_webauthn(postgresql::Database&, const Key&, const std::string& origin,
                               const FactorResponse& body);
} // namespace bacuri::vms::control
