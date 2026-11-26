#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace bacuri::desktop
{
// Distribution of measured values, such as frame intervals in milliseconds.
struct Distribution {
    std::size_t count{};
    double p50{};
    double p95{};
    double p99{};
    double max{};
};

// Nearest-rank percentiles. An empty input gives an empty distribution.
Distribution distribution(std::vector<double> values);

// Index of the frame shown at a presentation time for a constant frame rate, counted from zero.
std::int64_t frame_index(std::int64_t pts_ns, int rate_numerator, int rate_denominator);

// Where decoded frames live before the sink: in GPU memory or in system memory.
enum class MemoryPath { gpu, system };

// Classifies the features of negotiated video caps, such as "memory:GLMemory".
MemoryPath memory_path(std::string_view caps_features);

// Copies of every frame through system memory between the decoder and the display.
// GPU memory needs none, system memory needs one upload to the GPU.
int system_memory_copies(MemoryPath decoder_output);
} // namespace bacuri::desktop
