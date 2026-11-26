#pragma once
#include <optional>
#include "application/control_api/api.hpp"
#include "modules/inventory/registration_types.hpp"

namespace bacuri::vms::control::inventory
{
std::string register_device(postgresql::Database&, const std::string& site, const CameraRegistration& body);
void write_version(postgresql::Database&, const Key&, const std::string& site, const std::string& device,
                   const std::string& revision, const CameraVersion& body);
SelectedChannel channel_configuration(const Key&, const CameraSelection& selection, const std::string& main,
                                      const std::string& secondary);
std::optional<RegisteredCamera> registered_channel(postgresql::Database&, const std::string& site,
                                                   const SelectedChannel& config);
} // namespace bacuri::vms::control::inventory
