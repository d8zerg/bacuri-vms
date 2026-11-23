#pragma once

#include <string>

namespace bacuri::vms::control
{
class Database;
class ConfigurationSnapshot;
enum class StorageLayout;
} // namespace bacuri::vms::control

namespace bacuri::vms::control::media_sessions
{
void layout_configuration(ConfigurationSnapshot&);
std::string open_export(Database&, const std::string& browser, const std::string& site, const std::string& point,
                        const std::string& begin, const std::string& end);
void close_export(Database&, const std::string& session);
bool close_archive_sessions(Database&, const std::string& site, StorageLayout layout);
} // namespace bacuri::vms::control::media_sessions
