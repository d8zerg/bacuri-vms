#pragma once
#include "control/api.hpp"

namespace bacuri::vms::control
{
Json discover_onvif(const CameraNetworks& networks, const Key& key, const std::string& site);
Json inspect_onvif(const CameraNetworks& networks, const Key& key, const std::string& site, const Json& body);
Json read_camera_ticket(const Key& key, const std::string& site, const std::string& ticket);
} // namespace bacuri::vms::control
