#pragma once
#include "domain/image_region.hpp"

#include <string>

namespace bacuri::vms::postgresql
{
class QuerySession;
class ConfigurationSnapshot;
} // namespace bacuri::vms::postgresql

namespace bacuri::vms::control
{
enum class StorageLayout;
} // namespace bacuri::vms::control

namespace bacuri::vms::control::media_sessions
{
void layout_configuration(postgresql::ConfigurationSnapshot&);
struct ExportScope {
    std::string grant, begin, end;
    domain::ImageRegions regions;
    bool derivative{}, sound{};
};
std::string open_export(postgresql::QuerySession&, const std::string& browser, const std::string& site,
                        const std::string& point, const std::string& begin, const std::string& end,
                        const ExportScope& scope);
void close_export(postgresql::QuerySession&, const std::string& session);
bool close_archive_sessions(postgresql::QuerySession&, const std::string& site, StorageLayout layout);
} // namespace bacuri::vms::control::media_sessions
