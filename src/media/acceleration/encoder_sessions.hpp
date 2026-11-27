#pragma once
#include "media/acceleration/library.hpp"
#include <optional>

namespace bacuri::vms::acceleration
{
inline std::optional<unsigned> encoder_sessions(const Library& nvml, void* device)
{
    unsigned count{}, fps{}, latency{};
    if (nvml.symbol<int(void*, unsigned*, unsigned*, unsigned*)>("nvmlDeviceGetEncoderStats")(device, &count, &fps,
                                                                                              &latency) == 0)
        return count;
    return {};
}
} // namespace bacuri::vms::acceleration
