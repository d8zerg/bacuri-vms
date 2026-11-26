#pragma once
#include "application/control_api/api.hpp"
namespace bacuri::vms::control
{
inline void add_gap(Json& periods, std::int64_t a, std::int64_t b, const std::string& reason)
{
    if (b > a)
        periods.push_back({{"state", "gap"}, {"begin_utc_us", a}, {"end_utc_us", b}, {"reason", reason}});
}
} // namespace bacuri::vms::control
