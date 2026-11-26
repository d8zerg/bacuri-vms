#pragma once
#include <cstdint>
#include <string>
#include <memory>
#include <map>
#include <limits>
#include <vector>
#include <glib.h>
namespace bacuri::vms::recording
{
struct WeeklyPeriod {
    int day, start, end;
};
struct CaptureWindow {
    std::int64_t binding, begin, end;
};
enum class RecordingAudio { operator_lease, disabled, enabled };
struct CaptureRule {
    bool continuous{true}, manual{}, event{};
    RecordingAudio audio{RecordingAudio::operator_lease};
    int pre{}, post{}, days{};
    std::string id;
    std::int64_t revision{}, since{}, until{std::numeric_limits<std::int64_t>::max()};
    std::shared_ptr<GTimeZone> zone;
    std::vector<WeeklyPeriod> schedule;
    std::vector<CaptureWindow> windows;
    bool selects(std::int64_t utc, std::int64_t binding) const;
};
using PointPolicies = std::map<std::string, CaptureRule>;
} // namespace bacuri::vms::recording
