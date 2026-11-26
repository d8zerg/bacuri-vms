#pragma once
#include <string>

namespace bacuri::vms::postgresql
{
class QuerySession;
}

namespace bacuri::vms::control
{
bool point_on_plan(postgresql::QuerySession&, const std::string& site, const std::string& point,
                   const std::string& plan);
} // namespace bacuri::vms::control
