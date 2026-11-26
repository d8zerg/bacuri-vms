#pragma once

#include "domain/identifier.hpp"
#include "domain/time.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace bacuri::vms::domain
{

using Revision = std::int64_t;

struct Site {
    SiteId id;
    std::string name;
    std::string time_zone;
};

struct ObservationPoint {
    SiteId site;
    ObservationPointId id;
    std::string name;
};

struct Device {
    SiteId site;
    DeviceId id;
    std::string name;
};

struct Channel {
    SiteId site;
    DeviceId device;
    ChannelId id;
    std::int32_t index;
};

enum class StreamRole {
    main,
    secondary,
};

struct StreamProfile {
    SiteId site;
    ChannelId channel;
    StreamProfileId id;
    StreamRole role;
};

struct BindingKey {
    SiteId site;
    ObservationPointId point;
    Revision revision;

    [[nodiscard]] friend constexpr bool operator==(const BindingKey&, const BindingKey&) noexcept = default;
};

// Identifiers have no default value, so the type is built only with all fields.
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
struct ChannelBinding {
    BindingKey key;
    ChannelId channel;
    UtcTime valid_from;
};

enum class BindingError {
    different_site,
    non_increasing_time,
    invalid_revision,
};

[[nodiscard]] std::expected<ChannelBinding, BindingError>
replace_channel(const ChannelBinding& active, const Channel& replacement, UtcTime valid_from) noexcept;

struct ConfigurationProgress {
    Revision desired_revision{};
    std::optional<Revision> applied_revision;

    [[nodiscard]] bool converged() const noexcept
    {
        return applied_revision == desired_revision;
    }
};

} // namespace bacuri::vms::domain
