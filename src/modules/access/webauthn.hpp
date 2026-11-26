#include "modules/access/enrollment.hpp"
#pragma once
#include "application/control_api/api.hpp"

namespace bacuri::vms::control
{
Json begin_webauthn(postgresql::Database&, const Key&, const std::string& origin, const std::string& user,
                    const Json& login);
struct VerifiedFactor {
    std::string user;
    bool enrolled{};
};
VerifiedFactor finish_webauthn(postgresql::Database&, const Key&, const std::string& origin, const Json& body);
} // namespace bacuri::vms::control
