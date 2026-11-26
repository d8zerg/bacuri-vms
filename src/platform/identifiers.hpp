#pragma once

#include <string_view>

namespace bacuri::vms::platform
{
// True for a lowercase UUID in its canonical text form.
bool canonical_id(std::string_view value);
} // namespace bacuri::vms::platform
