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

class ProjectedTime {
public:
    [[nodiscard]] static ProjectedTime from_reception(
        UtcTime received_at, std::optional<SourceTime> source = std::nullopt
    ) noexcept;

    [[nodiscard]] static std::expected<ProjectedTime, TimeError> from_source(
        const ClockAnchor& anchor, SourceTime source, UtcTime received_at
    ) noexcept;

    [[nodiscard]] UtcTime utc() const noexcept { return projection_ ? projection_->utc : received_at_; }
    [[nodiscard]] UtcTime received_at() const noexcept { return received_at_; }
    [[nodiscard]] std::optional<SourceTime> source() const noexcept { return source_; }

    [[nodiscard]] std::optional<ClockEpochId> clock_epoch() const noexcept
    {
        return projection_ ? std::optional{projection_->epoch} : std::nullopt;
    }

    [[nodiscard]] std::optional<Microseconds> uncertainty() const noexcept
    {
        return projection_ ? std::optional{projection_->uncertainty} : std::nullopt;
    }

    [[nodiscard]] TimeBasis basis() const noexcept
    {
        return projection_ ? TimeBasis::source_clock : TimeBasis::reception;
    }

private:
    struct SourceProjection {
        UtcTime utc;
        ClockEpochId epoch;
        Microseconds uncertainty;
    };

    ProjectedTime(UtcTime received_at, std::optional<SourceTime> source,
        std::optional<SourceProjection> projection) noexcept
        : received_at_{received_at}, source_{source}, projection_{projection} {}

    UtcTime received_at_;
    std::optional<SourceTime> source_;
    std::optional<SourceProjection> projection_;
};

}  // namespace bacuri::vms::domain
