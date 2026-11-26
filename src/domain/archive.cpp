#include "domain/archive.hpp"

namespace bacuri::vms::domain
{

std::expected<RecordedInterval, RecordingError> RecordedInterval::make(BlockKey block, BindingKey binding,
                                                                       UtcInterval interval,
                                                                       ProjectedTime first_frame) noexcept
{
    if (block.site != binding.site) {
        return std::unexpected{RecordingError::different_site};
    }
    if (first_frame.utc() != interval.begin()) {
        return std::unexpected{RecordingError::first_frame_not_at_begin};
    }
    return RecordedInterval{block, binding, interval, first_frame};
}

std::string_view name(GapReason reason) noexcept
{
    switch (reason) {
    case GapReason::source_offline:
        return "source_offline";
    case GapReason::storage_failure:
        return "storage_failure";
    case GapReason::overload:
        return "overload";
    case GapReason::keyframe_interval:
        return "keyframe_interval";
    }
    return "storage_failure";
}

std::optional<GapReason> parse_gap_reason(std::string_view text) noexcept
{
    for (const auto reason : {GapReason::source_offline, GapReason::storage_failure, GapReason::overload,
                              GapReason::keyframe_interval})
        if (name(reason) == text)
            return reason;
    return {};
}

} // namespace bacuri::vms::domain
