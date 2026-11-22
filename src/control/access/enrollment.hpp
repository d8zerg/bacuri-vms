#pragma once
#include <string>

namespace bacuri::vms::control
{
enum class Enrollment { first, recovery };
std::string enrollment_code(const std::string& username, Enrollment mode = Enrollment::first);

} // namespace bacuri::vms::control
