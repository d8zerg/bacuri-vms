#pragma once

#include <optional>
#include <vector>
#include <string>
#include <map>
#include <cstdint>

namespace bacuri::vms::postgresql
{
class Database;
class QuerySession;
} // namespace bacuri::vms::postgresql

namespace bacuri::vms::control::inventory
{
std::map<std::string, std::int64_t> recording_bindings(postgresql::QuerySession&, const std::string& site);
bool lock_point(postgresql::Database&, const std::string& site, const std::string& point);
bool lock_site(postgresql::Database&, const std::string& site);
void ensure_site(postgresql::Database&, const std::string& site);
bool has_binding(postgresql::Database&, const std::string& site, const std::string& point,
                 const std::string& revision, bool current = false);
bool lock_camera_revision(postgresql::Database&, const std::string& site, const std::string& point,
                          const std::string& device, const std::string& revision);

bool site_exists(postgresql::Database&, const std::string& site);

bool point_exists(postgresql::Database&, const std::string& site, const std::string& point);

// Each point resolves to its current device. Samples belong to a specific configuration revision.
struct CameraPosition {
    double pan{};
    std::string space;
    std::optional<double> tilt, zoom, hfov;
    std::optional<bool> moving;
    std::optional<std::string> source_time;
};
struct CameraOrientation {
    std::string device_id, profile, revision;
    bool ptz{}, fresh{};
    std::string observed_at;
    std::optional<CameraPosition> position;
};
std::map<std::string, CameraOrientation> camera_orientations(postgresql::QuerySession&, const std::string& site,
                                                             const std::vector<std::string>& points);
} // namespace bacuri::vms::control::inventory
