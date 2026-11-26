#pragma once
#include "control/access/session.hpp"
#include <stop_token>
namespace bacuri::vms::control::access
{
Json oidc_provider(Database&, const Request&, const Key&, const std::string&);
Json external_identity(Database&, const Request&, const std::string&, const std::string&);
Json configure_provider(Database&, const Key&, const std::string& site, const Json&);
Json bind_identity(Database&, const std::string& site, const std::string& user, const Json&);
void refresh_identities(const std::stop_token&, const Key&);
} // namespace bacuri::vms::control::access
