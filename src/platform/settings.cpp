#include "platform/settings.hpp"

#include <cstdlib>
#include <stdexcept>

namespace bacuri::vms::platform
{
std::string setting(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        throw std::runtime_error(std::string{"Missing setting "} + name);
    }
    return value;
}

std::optional<std::string> optional_setting(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0')
        return std::nullopt;
    return value;
}
} // namespace bacuri::vms::platform
