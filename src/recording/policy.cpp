#include "recording/policy_json.hpp"
#include "recording/policy.hpp"
#include <glib.h>
#include <algorithm>
#include <chrono>
namespace bacuri::vms::recording
{
namespace
{
int number(const nlohmann::json& o, const char* field, int low, int high)
{
    const auto& n = o.at(field);
    if (!n.is_number_integer() || n < low || n > high)
        throw std::invalid_argument("Invalid recording policy number");
    return n.get<int>();
}
} // namespace
CaptureRule parse_capture_rule(const nlohmann::json& o)
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
nlohmann::json capture_rule_json(const CaptureRule& rule)
{
    using Json = nlohmann::json;
    Json schedule = Json::array();
    for (const auto& p : rule.schedule)
        schedule.push_back({{"day", p.day}, {"start", p.start}, {"end", p.end}});
    return {{"continuous", rule.continuous},
            {"manual", rule.manual},
            {"event", rule.event},
            {"audio", rule.audio == RecordingAudio::enabled},
            {"pre", rule.pre},
            {"post", rule.post},
            {"days", rule.days ? Json(rule.days) : Json(nullptr)},
            {"zone", rule.zone ? g_time_zone_get_identifier(rule.zone.get()) : "UTC"},
            {"schedule", std::move(schedule)}};
}
void to_json(nlohmann::json& out, const CaptureRule& rule)
{
    auto windows = nlohmann::json::array();
    for (const auto& w : rule.windows)
        windows.push_back({w.binding, w.begin, w.end});
    out = {{"id", rule.id},
           {"revision", rule.revision},
           {"since", rule.since},
           {"rule", capture_rule_json(rule)},
           {"windows", std::move(windows)}};
}
void from_json(const nlohmann::json& entry, CaptureRule& result)
{
    auto rule = parse_capture_rule(entry.at("rule"));
    rule.id = entry.at("id");
    rule.revision = entry.at("revision");
    rule.since = entry.at("since");
    for (const auto& w : entry.at("windows"))
        rule.windows.push_back({w.at(0), w.at(1), w.at(2)});
    result = std::move(rule);
}
PointPolicies decode_plan(const nlohmann::json& j)
{
    if (!j.is_object())
        throw std::invalid_argument("Invalid recording plan");
    return j.get<PointPolicies>();
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
} // namespace bacuri::vms::recording
