#include "domain/resources.hpp"

#include <limits>

namespace bacuri::vms::domain
{

std::expected<ChannelBinding, BindingError>
replace_channel(const ChannelBinding& active, const Channel& replacement, UtcTime valid_from) noexcept
{
    if (active.key.site != replacement.site) {
        return std::unexpected{BindingError::different_site};
    }
    if (valid_from <= active.valid_from) {
        return std::unexpected{BindingError::non_increasing_time};
    }
    if (active.key.revision <= 0 || active.key.revision == std::numeric_limits<Revision>::max()) {
        return std::unexpected{BindingError::invalid_revision};
    }
    return ChannelBinding{
        .key = {active.key.site, active.key.point, active.key.revision + 1},
        .channel = replacement.id,
        .valid_from = valid_from,
    };
}

} // namespace bacuri::vms::domain
