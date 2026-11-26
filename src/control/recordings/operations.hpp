#pragma once

#include "control/recordings/status.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bacuri::vms::control
{
class Database;
}

namespace bacuri::vms::control::recordings
{
struct CoverageInterval {
    std::int64_t begin, end;
    std::string state, time_basis, uncertainty, reason;
};
struct ManifestPart {
    std::string initialization, media;
    std::string codec;
    std::int64_t begin, end;
};
struct DeliveryPart {
    std::string block, sha256;
    bool initialization;
    std::int64_t begin, end;
};
struct RecordedInterval {
    std::int64_t begin, end;
    std::string revision, basis, uncertainty, unavailable_reason;
};
struct PreviousRecording {
    std::string revision;
    std::int64_t start, end;
};
// The newest available block of each listed point. Points without recordings are absent.
std::map<std::string, LastRecorded> last_recorded(Database&, const std::string& site,
                                                  const std::vector<std::string>& points);
std::optional<PreviousRecording> previous_binding_recording(Database&, const std::string& site,
                                                            const std::string& point, const std::string& revision);
std::vector<CoverageInterval> edge_gaps(Database&, const std::string& site, const std::string& point,
                                        const std::string& binding, std::int64_t since, std::int64_t until);
void require_available(Database&, const std::string& site);
std::int64_t export_lifetime_ms(Database&, const std::string& site, const std::string& point,
                                const std::string& block_hashes);
std::vector<std::string> manifests_for_export(Database&, const std::string& site, const std::string& point,
                                              const std::string& begin, const std::string& end,
                                              const std::string& revision);
std::vector<CoverageInterval> coverage(Database&, const std::string& site, const std::string& point,
                                       const std::string& begin, const std::string& end,
                                       const std::string& pending, const std::string& revision);

std::int64_t retention_cutoff(Database&, const std::string& site, const std::string& point);

bool bind_playback_parts(Database& db, const std::string& session, const std::string& site,
                         const std::string& point, const std::string& begin, const std::string& end,
                         const std::string& revision);

std::vector<ManifestPart> playback_manifest(Database& db, const std::string& session);

std::optional<DeliveryPart> playback_part(Database& db, const std::string& session, const std::string& part);

std::vector<RecordedInterval> blocks_at(Database& db, const std::string& site, const std::string& point,
                                        const std::string& begin, const std::string& end);

std::optional<std::int64_t> delivery_lifetime_ms(Database&, const std::string& site, const std::string& point,
                                                 std::int64_t end);

std::string recovery_status(Database&, const std::string& site);

void change_retention(Database&, const std::string& site, int days, int reserve);
void set_retention(Database& db, const std::string& site, int days, int reserve);

void await_verification(Database& db, const std::string& site);
} // namespace bacuri::vms::control::recordings
