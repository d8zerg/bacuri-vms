#pragma once
#include "application/control_api/http_json.hpp"
#include "modules/events/alerts.hpp"
#include "application/control_api/playback_time_json.hpp"
namespace bacuri::vms::control
{
inline void to_json(Json& j, const Alert& a)
{
    j = {{"id", a.id},
         {"point_id", a.point_id},
         {"type", a.type},
         {"work_state", a.work_state},
         {"time_basis", a.time_basis},
         {"observed_at", utc_text(a.observed_at)},
         {"archive_interval", alert_window(a.observed_at)}};
    if (a.uncertainty)
        j["uncertainty_us"] = std::to_string(*a.uncertainty);
}
inline void to_json(Json& j, const AlertSnapshot& s)
{
    j = {{"items", s.items}, {"cursor", s.cursor}};
}
inline void to_json(Json& j, const AlertRemoved& f)
{
    j = {{"type", "remove"}, {"alert_id", f.alert_id}, {"cursor", f.cursor}};
}
inline void to_json(Json& j, const AlertUpdated& f)
{
    j = {{"type", "upsert"}, {"alert", f.alert}, {"cursor", f.cursor}};
}
inline void to_json(Json& j, const ResyncRequired& f)
{
    j = {{"type", "resync_required"}, {"reason", f.reason}};
}
inline std::string encode_frame(const AlertFrame& frame)
{
    return std::visit([](const auto& f) { return Json(f).dump(); }, frame);
}
} // namespace bacuri::vms::control
