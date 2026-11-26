#pragma once
#include <optional>
#include "application/export/package.hpp"
#include <variant>
namespace bacuri::vms::control
{
struct ExportGap {
    std::int64_t begin{}, end{};
    std::string reason;
};
struct ExportPresentation {
    std::string codec;
    domain::ImageRegions masks;
    std::optional<ExportedAudio> audio;
};
struct ExportClip {
    archive::BlockManifest source;
    std::string file, transform;
    archive::MediaDigest digest;
    std::int64_t begin{}, end{};
    std::optional<std::int64_t> frame_count;
    std::optional<ExportPresentation> presentation;
};
using ExportPeriod = std::variant<ExportGap, ExportClip>;
struct FragmentManifest {
    std::string site_id, point_id, export_id, alert_id;
    std::int64_t begin_us{}, end_us{};
    std::string mode, reason;
    bool audio{};
    std::vector<ExportPeriod> periods;
};
// The exact bytes written to manifest.json are also the signed payload.
std::string encode_fragment_manifest(const FragmentManifest& manifest);
inline void add_gap(std::vector<ExportPeriod>& periods, std::int64_t begin, std::int64_t end,
                    const std::string& reason)
{
    if (end > begin)
        periods.emplace_back(ExportGap{begin, end, reason});
}
} // namespace bacuri::vms::control
