#pragma once

#include <optional>
#include <string>

namespace bacuri::vms::platform
{
// A required setting of the process environment. Throws when it is missing or empty.
std::string setting(const char* name);
// An optional setting of the process environment. Empty when it is missing or empty.
std::optional<std::string> optional_setting(const char* name);
} // namespace bacuri::vms::platform
