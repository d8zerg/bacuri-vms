#pragma once

#include <string>

namespace bacuri::vms::control
{
class Database;
}

namespace bacuri::vms::control::inventory
{
bool lock_point(Database&, const std::string& site, const std::string& point);
bool lock_site(Database&, const std::string& site);
void ensure_site(Database&, const std::string& site);
bool has_binding(Database&, const std::string& site, const std::string& point, const std::string& revision);
bool lock_camera_revision(Database&, const std::string& site, const std::string& point, const std::string& device,
                          const std::string& revision);

bool site_exists(Database&, const std::string& site);

bool point_exists(Database&, const std::string& site, const std::string& point);
} // namespace bacuri::vms::control::inventory
