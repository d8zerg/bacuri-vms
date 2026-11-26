#pragma once
#include "application/control_api/http_json.hpp"
#include "application/control_api/playback_time_json.hpp"
#include "modules/media_sessions/playback_operations.hpp"
namespace bacuri::vms::control::playback
{
inline void to_json(Json& j, const ArchivePage& p)
{
    j = {{"items", Json::array()}};
    for (const auto& row : p.items) {
        Json entry{{"interval", Window{row.begin, row.end}}, {"state", row.state}};
        if (!row.time_basis.empty())
            entry["time_basis"] = row.time_basis;
        if (!row.uncertainty.empty())
            entry["uncertainty_us"] = row.uncertainty;
        if (!row.reason.empty())
            entry["reason"] = row.reason;
        j["items"].push_back(std::move(entry));
    }
    if (p.next_cursor)
        j["next_cursor"] = *p.next_cursor;
}
inline void from_json(const Json& j, PresentationOptions& p)
{
    p = {};
    if (j.contains("quality"))
        p.quality = j.at("quality").get<std::string>();
    if (j.contains("hevc"))
        p.hevc = j.at("hevc").get<bool>();
    if (j.contains("accept_masked"))
        p.accept_masked = j.at("accept_masked").get<bool>();
    if (j.contains("cell_width"))
        p.cell_width = j.at("cell_width").get<unsigned>();
    if (j.contains("bandwidth_kbps"))
        p.bandwidth_kbps = j.at("bandwidth_kbps").get<unsigned>();
}
inline void to_json(Json& j, const PresentationOptions& p)
{
    j = Json::object();
    if (p.quality)
        j["quality"] = *p.quality;
    if (p.hevc)
        j["hevc"] = *p.hevc;
    if (p.accept_masked)
        j["accept_masked"] = *p.accept_masked;
    if (p.cell_width)
        j["cell_width"] = *p.cell_width;
    if (p.bandwidth_kbps)
        j["bandwidth_kbps"] = *p.bandwidth_kbps;
}
inline void to_json(Json& j, const VideoProfile& p)
{
    j = {{"profile", p.profile},    {"codec", p.codec},   {"source_codec", p.source_codec},
         {"width", p.width},        {"height", p.height}, {"bitrate_kbps", p.bitrate_kbps},
         {"converted", p.converted}};
    if (p.reason)
        j["reason"] = *p.reason;
}
inline void from_json(const Json& j, VideoProfile& p)
{
    p = {j.at("profile"), j.at("source_codec"), j.at("codec"),     j.at("width"),
         j.at("height"),  j.at("bitrate_kbps"), j.at("converted"), {}};
    if (j.contains("reason"))
        p.reason = j.at("reason").get<std::string>();
}
inline void to_json(Json& j, const SessionSelection& s)
{
    j = s.options;
    j["mode"] = s.mode;
    if (s.interval)
        j["interval"] = *s.interval;
    if (s.binding)
        j["binding_revision"] = std::to_string(*s.binding);
}
inline void to_json(Json& j, const MediaSession& s)
{
    j = {{"id", s.id}, {"point_id", s.point_id}, {"expires_at", s.expires_at}, {"selection", s.selection}};
    if (s.video)
        j["video"] = *s.video;
}
} // namespace bacuri::vms::control::playback
