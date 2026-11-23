#pragma once
#include "control/api.hpp"

namespace bacuri::vms::control::inventory
{
std::string register_device(Database&, const std::string& site, const Json& body);
void write_version(Database&, const Key&, const std::string& site, const std::string& device,
                   const std::string& revision, const Json& body);
Json channel_configuration(const Key&, const Json& selection, const std::string& main,
                           const std::string& secondary);
Json registered_channel(Database&, const std::string& site, const Json& config);
} // namespace bacuri::vms::control::inventory
