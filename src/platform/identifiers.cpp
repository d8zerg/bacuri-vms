#include "platform/identifiers.hpp"
#include "domain/identifier.hpp"

namespace bacuri::vms::platform
{
bool canonical_id(std::string_view value)
{
    const auto id = domain::DeviceId::parse(value);
    return id && id->to_string() == value;
}

} // namespace bacuri::vms::platform
