#pragma once

#include "domain/identifier.hpp"

#include <chrono>
#include <expected>
#include <optional>

namespace bacuri::vms::domain {

using Microseconds = std::chrono::microseconds;
using UtcTime = std::chrono::sys_time<Microseconds>;

struct SourceTime {
    Microseconds value;

    [[nodiscard]] friend constexpr bool operator==(const SourceTime&, const SourceTime&) noexcept = default;
};

enum class TimeError {
    empty_or_reversed_interval,
    negative_uncertainty,
    arithmetic_overflow,
};

class UtcInterval {
public:
    [[nodiscard]] static std::expected<UtcInterval, TimeError> make(UtcTime begin, UtcTime end) noexcept;

    [[nodiscard]] constexpr UtcTime begin() const noexcept { return begin_; }
    [[nodiscard]] constexpr UtcTime end() const noexcept { return end_; }

    [[nodiscard]] constexpr bool contains(UtcTime time) const noexcept
    {
        return begin_ <= time && time < end_;
    }

private:
    constexpr UtcInterval(UtcTime begin, UtcTime end) noexcept : begin_{begin}, end_{end} {}

    UtcTime begin_;
    UtcTime end_;
};

struct ClockAnchor {
    ClockEpochId epoch;
    SourceTime source;
    UtcTime utc;
    Microseconds uncertainty;
};

enum class TimeBasis {
    source_clock,
    reception,
};

struct ProjectedTime {
    UtcTime utc;
    UtcTime received_at;
    std::optional<SourceTime> source;
    std::optional<ClockEpochId> clock_epoch;
    std::optional<Microseconds> uncertainty;

    [[nodiscard]] TimeBasis basis() const noexcept
    {
        return clock_epoch.has_value() ? TimeBasis::source_clock : TimeBasis::reception;
    }
};

[[nodiscard]] ProjectedTime estimate_from_reception(
    UtcTime received_at, std::optional<SourceTime> source = std::nullopt
) noexcept;

[[nodiscard]] std::expected<ProjectedTime, TimeError> project_from_source(
    const ClockAnchor& anchor, SourceTime source, UtcTime received_at
) noexcept;

}  // namespace bacuri::vms::domain
