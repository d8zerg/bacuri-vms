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

} // namespace bacuri::vms::domain
