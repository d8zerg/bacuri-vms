#pragma once

#include "domain/resources.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace bacuri::vms::domain {

struct ObservationKey {
    SiteId site;
    ObservationSourceId source;
    ObservationId id;

    [[nodiscard]] friend constexpr bool operator==(const ObservationKey&, const ObservationKey&) noexcept = default;
};

struct Observation {
    ObservationKey key;
    std::string type;
    std::int32_t schema_version;
    ProjectedTime time;
    std::optional<BindingKey> binding;
    std::string origin;
    std::string attributes_json;
};

}  // namespace bacuri::vms::domain
