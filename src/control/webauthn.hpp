#pragma once
#include "control/api.hpp"

namespace bacuri::vms::control
{
enum class Enrollment { first, recovery };
std::string enrollment_code(const std::string& username, Enrollment mode = Enrollment::first);
Json begin_webauthn(Database&, const Key&, const std::string& origin, const std::string& user, const Json& login);
struct VerifiedFactor {
    std::string user;
    bool enrolled;
};
VerifiedFactor finish_webauthn(Database&, const Key&, const std::string& origin, const Json& body);
} // namespace bacuri::vms::control
