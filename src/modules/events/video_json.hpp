#pragma once
#include "application/control_api/http_json.hpp"
#include "modules/events/video_rules.hpp"
#include "application/control_api/playback_time.hpp"
namespace bacuri::vms::control::events
{
inline void from_json(const Json& j, MotionAttributes& a)
{
    a = {};
    if (j.contains("topic"))
        a.topic = j.at("topic").get<std::string>();
    a.cells = j.value("cells", std::string{});
    a.previous = j.value("previous", std::string{});
}
// The HTTP projection reads vendor-defined data only for events visible in this page.
Json video_events_json(postgresql::QuerySession&, const std::string& site, const std::string& point,
                       const std::vector<VideoEvent>& events);
} // namespace bacuri::vms::control::events
