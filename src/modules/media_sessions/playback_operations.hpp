#pragma once
#include <optional>
#include "application/control_api/playback_time.hpp"
#include "modules/recordings/operations.hpp"

namespace bacuri::vms::control::playback
{
struct PointAccess {
    std::string site, point, browser, role, user;
};
struct SearchQuery {
    Window interval{};
    std::optional<std::string> cursor;
    bool masked{};
};
struct ArchivePage {
    std::vector<recordings::CoverageInterval> items;
    std::optional<std::string> next_cursor;
};
struct PresentationOptions {
    std::optional<std::string> quality;
    std::optional<bool> hevc, accept_masked;
    std::optional<unsigned> cell_width, bandwidth_kbps;
    bool empty() const
    {
        return !quality && !hevc && !accept_masked && !cell_width && !bandwidth_kbps;
    }
    bool operator==(const PresentationOptions&) const = default;
};
struct SessionSelection {
    std::string mode;
    std::optional<Window> interval;
    std::optional<std::int64_t> binding;
    PresentationOptions options;
};
struct VideoProfile {
    std::string profile, source_codec, codec;
    unsigned width{}, height{}, bitrate_kbps{};
    bool converted{};
    std::optional<std::string> reason;
};
struct MediaSession {
    std::string id, point_id, expires_at;
    SessionSelection selection;
    std::optional<VideoProfile> video;
};
ArchivePage search(postgresql::Database& db, const Key& key, const PointAccess& access, const SearchQuery& query,
                   std::optional<std::int64_t> binding = {});
MediaSession create(postgresql::Database& db, const Key& key, const PointAccess& access,
                    const SessionSelection& selection, const std::string& request_key);
} // namespace bacuri::vms::control::playback
