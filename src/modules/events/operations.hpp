#pragma once
#include "domain/acceleration.hpp"

#include <variant>

#include <cstdint>
#include <optional>
#include <string>

namespace bacuri::vms::postgresql
{
class Database;
class QuerySession;
} // namespace bacuri::vms::postgresql

namespace bacuri::vms::control::events
{
struct NodeAccelerationChange {
    std::string node;
    domain::AccelerationSettings previous, next;
    std::int64_t revision{};
};
void record_acceleration_change(postgresql::QuerySession&, const std::string& site, const std::string& user,
                                const NodeAccelerationChange&);
struct ArchiveAnchor {
    std::string point, revision, basis;
    std::int64_t time;
    std::string uncertainty;
};
std::optional<ArchiveAnchor> archive_anchor(postgresql::QuerySession&, const std::string& site,
                                            const std::string& alert);
void record_camera_control(postgresql::Database&, const std::string& site, const std::string& point,
                           const std::string& user, const std::string& outcome, const std::string& command,
                           const std::string& target);
struct PolicyRevision {
    std::string policy, revision;
};
struct ManualDuration {
    int seconds{};
};
struct StorageSettings {
    int days{}, reserve_percent{};
};
using RecordingChange = std::variant<std::monostate, PolicyRevision, ManualDuration, StorageSettings>;
struct FragmentExportAudit {
    std::string id, start, end, mode, reason;
    bool audio{};
};
struct CaseExportAudit {
    std::string case_id, revision, mode, start, end;
};
using ExportAudit = std::variant<FragmentExportAudit, CaseExportAudit>;
void record_recording_change(postgresql::QuerySession& db, const std::string& site, const std::string& point,
                             const std::string& user, const std::string& action, const std::string& change,
                             const RecordingChange& details = {});
void record_export(postgresql::Database&, const std::string& site, const std::string& point,
                   const std::string& user, const std::string& alert, const std::string& outcome,
                   const std::string& reason, const ExportAudit& details);
} // namespace bacuri::vms::control::events
