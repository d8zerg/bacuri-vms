#include "domain/time.hpp"

#include <limits>

namespace bacuri::vms::domain {

std::expected<UtcInterval, TimeError> UtcInterval::make(UtcTime begin, UtcTime end) noexcept
{
    if (end <= begin) {
        return std::unexpected{TimeError::empty_or_reversed_interval};
    }
    return UtcInterval{begin, end};
}

ProjectedTime estimate_from_reception(UtcTime received_at, std::optional<SourceTime> source) noexcept
{
    return {
        .utc = received_at,
        .received_at = received_at,
        .source = source,
        .clock_epoch = std::nullopt,
        .uncertainty = std::nullopt,
    };
}

std::expected<ProjectedTime, TimeError> project_from_source(
    const ClockAnchor& anchor, SourceTime source, UtcTime received_at
) noexcept
{
    if (anchor.uncertainty < Microseconds::zero()) {
        return std::unexpected{TimeError::negative_uncertainty};
    }

    constexpr auto minimum = std::numeric_limits<Microseconds::rep>::min();
    constexpr auto maximum = std::numeric_limits<Microseconds::rep>::max();
    const auto source_count = source.value.count();
    const auto source_anchor = anchor.source.value.count();

    if ((source_anchor > 0 && source_count < minimum + source_anchor)
        || (source_anchor < 0 && source_count > maximum + source_anchor)) {
        return std::unexpected{TimeError::arithmetic_overflow};
    }

    const auto offset = source_count - source_anchor;
    const auto utc_anchor = anchor.utc.time_since_epoch().count();
    if ((offset > 0 && utc_anchor > maximum - offset) || (offset < 0 && utc_anchor < minimum - offset)) {
        return std::unexpected{TimeError::arithmetic_overflow};
    }

    return ProjectedTime{
        .utc = UtcTime{Microseconds{utc_anchor + offset}},
        .received_at = received_at,
        .source = source,
        .clock_epoch = anchor.epoch,
        .uncertainty = anchor.uncertainty,
    };
}

}  // namespace bacuri::vms::domain
