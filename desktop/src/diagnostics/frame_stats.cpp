#include "diagnostics/frame_stats.hpp"

#include <algorithm>
#include <cmath>

namespace bacuri::vms::client
{
namespace
{
double rank(const std::vector<double>& sorted, double percentile)
{
    const auto position =
        static_cast<std::size_t>(std::ceil(percentile / 100.0 * static_cast<double>(sorted.size())));
    return sorted[std::clamp<std::size_t>(position, 1, sorted.size()) - 1];
}
} // namespace

Distribution distribution(std::vector<double> values)
{
    if (values.empty())
        return {};
    std::ranges::sort(values);
    return {values.size(), rank(values, 50), rank(values, 95), rank(values, 99), values.back()};
}

std::int64_t frame_index(std::int64_t pts_ns, int rate_numerator, int rate_denominator)
{
    if (rate_numerator <= 0 || rate_denominator <= 0 || pts_ns < 0)
        return -1;
    const auto frames = static_cast<long double>(pts_ns) * rate_numerator / (1e9L * rate_denominator);
    return static_cast<std::int64_t>(std::llround(frames));
}

MemoryPath memory_path(std::string_view caps_features)
{
    for (const auto gpu : {"memory:GLMemory", "memory:CUDAMemory", "memory:VAMemory", "memory:DMABuf"}) {
        if (caps_features.contains(gpu))
            return MemoryPath::gpu;
    }
    return MemoryPath::system;
}

int system_memory_copies(MemoryPath decoder_output)
{
    return decoder_output == MemoryPath::gpu ? 0 : 1;
}
} // namespace bacuri::vms::client
