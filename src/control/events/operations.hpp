#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace bacuri::vms::control
{
class Database;
}

namespace bacuri::vms::control::events
{
struct ArchiveAnchor {
    std::string point, revision, basis;
    std::int64_t time;
    std::string uncertainty;
};
std::optional<ArchiveAnchor> archive_anchor(Database&, const std::string& site, const std::string& alert);
void record_camera_control(Database&, const std::string& site, const std::string& point, const std::string& user,
                           const std::string& outcome, const std::string& command, const std::string& target);
void record_recording_change(Database& db, const std::string& site, const std::string& point,
                             const std::string& user, const std::string& action, const std::string& reason);
void record_export(Database&, const std::string& site, const std::string& point, const std::string& user,
                   const std::string& alert, const std::string& outcome, const std::string& reason,
                   const std::string& details);
} // namespace bacuri::vms::control::events
