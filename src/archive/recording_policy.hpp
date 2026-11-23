#pragma once
#include "archive/manifest.hpp"
#include <memory>
#include <limits>
#include <vector>
struct _GTimeZone;
namespace bacuri::vms::archive
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
    std::shared_ptr<_GTimeZone> zone;
    std::vector<WeeklyPeriod> schedule;
    std::vector<CaptureWindow> windows;
    bool selects(std::int64_t utc, std::int64_t binding) const;
};
CaptureRule parse_capture_rule(const Json& rule);
} // namespace bacuri::vms::archive
