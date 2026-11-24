#include "archive/recording_policy.hpp"
#include <glib.h>
#include <algorithm>
namespace bacuri::vms::archive
{
namespace
{
int number(const Json& o, const char* field, int low, int high)
{
    const auto& n = o.at(field);
    if (!n.is_number_integer() || n < low || n > high)
        throw std::invalid_argument("Invalid recording policy number");
    return n.get<int>();
}
} // namespace
CaptureRule parse_capture_rule(const Json& o)
{
    CaptureRule r;
    if (!o.is_object() || o.size() != 9)
        throw std::invalid_argument("Invalid recording policy fields");
    r.continuous = o.at("continuous").get<bool>();
    r.manual = o.at("manual").get<bool>();
    r.event = o.at("event").get<bool>();
    r.audio = o.at("audio").get<bool>() ? RecordingAudio::enabled : RecordingAudio::disabled;
    r.pre = number(o, "pre", 0, 30);
    r.post = number(o, "post", 1, 300);
    r.days = o.at("days").is_null() ? 0 : number(o, "days", 1, 36500);
    auto name = o.at("zone").get<std::string>();
    if (name.empty() || name.size() > 96 ||
        name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/+_-") != name.npos)
        throw std::invalid_argument("Invalid recording time zone");
    auto* zone = g_time_zone_new_identifier(name.c_str());
    if (!zone)
        throw std::invalid_argument("Unknown recording time zone");
    r.zone = {zone, g_time_zone_unref};
    const auto& periods = o.at("schedule");
    if (!periods.is_array() || periods.size() > 56)
        throw std::invalid_argument("Invalid weekly schedule");
    for (const auto& p : periods) {
        WeeklyPeriod s{number(p, "day", 1, 7), number(p, "start", 0, 1439), number(p, "end", 1, 1440)};
        if (s.start == s.end || p.size() != 3)
            throw std::invalid_argument("Invalid weekly period");
        r.schedule.push_back(s);
    }
    return r;
}
bool CaptureRule::selects(std::int64_t utc, std::int64_t binding) const
{
    if (utc < since || utc >= until)
        return false;
    if (continuous || std::ranges::any_of(windows, [&](const auto& w) {
            return w.binding == binding && w.begin <= utc && utc < w.end;
        }))
        return true;
    if (schedule.empty())
        return false;
    // Evaluate UTC instants in the selected zone. A repeated civil hour occurs twice.
    const auto seconds = std::chrono::floor<std::chrono::seconds>(std::chrono::microseconds{utc}).count();
    const auto interval = g_time_zone_find_interval(zone.get(), G_TIME_TYPE_UNIVERSAL, seconds);
    const std::chrono::sys_seconds local{
        std::chrono::seconds{seconds + g_time_zone_get_offset(zone.get(), interval)}};
    const auto day = std::chrono::floor<std::chrono::days>(local);
    const int weekday = static_cast<int>(std::chrono::weekday{day}.iso_encoding());
    const auto minute = std::chrono::duration_cast<std::chrono::minutes>(local - day).count();
    return std::ranges::any_of(schedule, [&](const auto& p) {
        if (p.start < p.end)
            return p.day == weekday && p.start <= minute && minute < p.end;
        return (p.day == weekday && minute >= p.start) || (p.day % 7 + 1 == weekday && minute < p.end);
    });
}
} // namespace bacuri::vms::archive
