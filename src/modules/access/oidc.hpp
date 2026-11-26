#pragma once
#include "modules/access/session.hpp"
#include <stop_token>
namespace bacuri::vms::control::access
{
Json oidc_provider(postgresql::Database&, const Request&, const Key&, const std::string&);
Json external_identity(postgresql::Database&, const Request&, const std::string&, const std::string&);
Json configure_provider(postgresql::Database&, const Key&, const std::string& site, const Json&);
Json bind_identity(postgresql::Database&, const std::string& site, const std::string& user, const Json&);
void refresh_identities(const std::stop_token&, const Key&);
} // namespace bacuri::vms::control::access
