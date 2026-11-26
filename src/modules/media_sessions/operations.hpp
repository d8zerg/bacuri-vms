#pragma once

#include <string>

namespace bacuri::vms::postgresql
{
class Database;
class ConfigurationSnapshot;
} // namespace bacuri::vms::postgresql

namespace bacuri::vms::control
{
enum class StorageLayout;
} // namespace bacuri::vms::control

namespace bacuri::vms::control::media_sessions
{
void layout_configuration(postgresql::ConfigurationSnapshot&);
std::string open_export(postgresql::Database&, const std::string& browser, const std::string& site,
                        const std::string& point, const std::string& begin, const std::string& end,
                        const std::string& presentation);
void close_export(postgresql::Database&, const std::string& session);
bool close_archive_sessions(postgresql::Database&, const std::string& site, StorageLayout layout);
} // namespace bacuri::vms::control::media_sessions
