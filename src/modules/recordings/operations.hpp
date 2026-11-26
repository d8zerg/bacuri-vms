#pragma once

#include "archive/manifest.hpp"
#include "modules/recordings/status.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bacuri::vms::postgresql
{
class Database;
class QuerySession;
} // namespace bacuri::vms::postgresql

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
std::map<std::string, LastRecorded> last_recorded(postgresql::Database&, const std::string& site,
                                                  const std::vector<std::string>& points);
std::optional<PreviousRecording> previous_binding_recording(postgresql::Database&, const std::string& site,
                                                            const std::string& point, const std::string& revision);
std::vector<CoverageInterval> edge_gaps(postgresql::Database&, const std::string& site, const std::string& point,
                                        const std::string& binding, std::int64_t since, std::int64_t until);
void require_available(postgresql::Database&, const std::string& site);
std::int64_t export_lifetime_ms(postgresql::Database&, const std::string& site, const std::string& point,
                                const std::string& block_hashes);
std::vector<archive::BlockManifest> manifests_for_export(postgresql::Database&, const std::string& site,
                                                         const std::string& point, const std::string& begin,
                                                         const std::string& end, const std::string& revision);
std::vector<CoverageInterval> coverage(postgresql::Database&, const std::string& site, const std::string& point,
                                       const std::string& begin, const std::string& end,
                                       const std::string& pending, const std::string& revision);

std::int64_t retention_cutoff(postgresql::Database&, const std::string& site, const std::string& point);

bool bind_playback_parts(postgresql::Database& db, const std::string& session, const std::string& site,
                         const std::string& point, const std::string& begin, const std::string& end,
                         const std::string& revision);

std::vector<ManifestPart> playback_manifest(postgresql::Database& db, const std::string& session);

std::optional<DeliveryPart> playback_part(postgresql::Database& db, const std::string& session,
                                          const std::string& part);

std::vector<RecordedInterval> blocks_at(postgresql::Database& db, const std::string& site,
                                        const std::string& point, const std::string& begin,
                                        const std::string& end);

std::optional<std::int64_t> delivery_lifetime_ms(postgresql::Database&, const std::string& site,
                                                 const std::string& point, std::int64_t end);

std::string recovery_status(postgresql::Database&, const std::string& site);

void change_retention(postgresql::Database&, const std::string& site, int days, int reserve);
void set_retention(postgresql::Database& db, const std::string& site, int days, int reserve);

void await_verification(postgresql::Database& db, const std::string& site);

struct MotionSample {
    std::int64_t offset;
    std::string cells, image;
};
struct MotionIndex {
    std::vector<std::int64_t> frames;
    std::vector<MotionSample> samples;
};
struct IndexedBlock {
    std::int64_t begin, end;
    std::string basis, binding;
    std::optional<std::int64_t> uncertainty;
    std::optional<MotionIndex> index;
};
std::vector<IndexedBlock> indexed_blocks(postgresql::QuerySession&, const std::string& site,
                                         const std::string& point, std::int64_t begin, std::int64_t end,
                                         int limit);
} // namespace bacuri::vms::control::recordings
