#pragma once
#include "application/control_api/api.hpp"

namespace bacuri::vms::control::inventory
{
std::string register_device(postgresql::Database&, const std::string& site, const Json& body);
void write_version(postgresql::Database&, const Key&, const std::string& site, const std::string& device,
                   const std::string& revision, const Json& body);
Json channel_configuration(const Key&, const Json& selection, const std::string& main,
                           const std::string& secondary);
Json registered_channel(postgresql::Database&, const std::string& site, const Json& config);
} // namespace bacuri::vms::control::inventory
