#pragma once
#include <string>
#include <vector>

namespace bacuri::vms::domain
{
struct AccelerationSettings {
    std::string mode{"cpu_only"};
    std::vector<std::string> gpu_tasks;
    bool operator==(const AccelerationSettings&) const = default;
};
} // namespace bacuri::vms::domain
