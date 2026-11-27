#include "diagnostics/frame_stats.hpp"

#include <gtest/gtest.h>

namespace bacuri::vms::client
{
namespace
{
TEST(Distribution, EmptyInputGivesNoValues)
{
    const auto result = distribution({});
    EXPECT_EQ(result.count, 0U);
    EXPECT_EQ(result.max, 0.0);
}

TEST(Distribution, UsesNearestRank)
{
    std::vector<double> values;
    for (int value = 100; value >= 1; --value)
        values.push_back(value);
    const auto result = distribution(values);
    EXPECT_EQ(result.count, 100U);
    EXPECT_EQ(result.p50, 50.0);
    EXPECT_EQ(result.p95, 95.0);
    EXPECT_EQ(result.p99, 99.0);
    EXPECT_EQ(result.max, 100.0);
}

TEST(Distribution, SingleValueIsEveryPercentile)
{
    const auto result = distribution({16.7});
    EXPECT_EQ(result.p50, 16.7);
    EXPECT_EQ(result.p99, 16.7);
}

TEST(FrameIndex, RoundsToTheNearestFrame)
{
    EXPECT_EQ(frame_index(0, 25, 1), 0);
    EXPECT_EQ(frame_index(40'000'000, 25, 1), 1);
    EXPECT_EQ(frame_index(39'999'000, 25, 1), 1);
    EXPECT_EQ(frame_index(30'000'000'000, 25, 1), 750);
    EXPECT_EQ(frame_index(1'001'000'000, 30000, 1001), 30);
}

TEST(FrameIndex, RejectsInvalidInput)
{
    EXPECT_EQ(frame_index(-1, 25, 1), -1);
    EXPECT_EQ(frame_index(0, 0, 1), -1);
    EXPECT_EQ(frame_index(0, 25, 0), -1);
}

TEST(MemoryPath, GpuMemoryNeedsNoCopy)
{
    for (const auto features : {"memory:GLMemory", "memory:CUDAMemory", "memory:VAMemory", "memory:DMABuf"}) {
        EXPECT_EQ(memory_path(features), MemoryPath::gpu) << features;
        EXPECT_EQ(system_memory_copies(memory_path(features)), 0) << features;
    }
}

TEST(MemoryPath, SystemMemoryNeedsOneUpload)
{
    EXPECT_EQ(memory_path(""), MemoryPath::system);
    EXPECT_EQ(memory_path("memory:SystemMemory"), MemoryPath::system);
    EXPECT_EQ(system_memory_copies(MemoryPath::system), 1);
}
} // namespace
} // namespace bacuri::vms::client
