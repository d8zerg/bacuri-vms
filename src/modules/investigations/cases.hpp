#pragma once
#include "application/control_api/api.hpp"
namespace bacuri::vms::postgresql
{
class ConfigurationSnapshot;
}

namespace bacuri::vms::control
{
class Sessions;
namespace investigations
{
void case_configuration(postgresql::ConfigurationSnapshot&);
Json case_for_export(postgresql::Database&, const std::string& site, const std::string& id,
                     const std::string& user);
void investigation(const Request&, Response&, const Sessions&, const std::string& site, const std::string& id);
void case_recipients(const Request&, Response&, const Sessions&, const std::string& site);
} // namespace investigations
} // namespace bacuri::vms::control
