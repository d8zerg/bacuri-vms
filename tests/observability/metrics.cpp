#include "observability/metrics.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <random>

namespace bacuri::vms::observability
{
namespace
{
using namespace std::chrono_literals;

TEST(Metrics, HistogramExposesCumulativeBucketsSumAndCount)
{
    Registry registry;
    constexpr double bounds[] = {0.1, 0.5};
    auto& latency =
        registry.histogram({.name = "bacuri_test_seconds", .help = "Test latency."}, bounds, {{"kind", "live"}});
    // Binary fractions keep the sum exact. A value equal to a bound belongs to that bucket.
    for (const double seconds : {0.0625, 0.25, 0.5, 2.0})
        latency.observe(std::chrono::duration<double>{seconds});
    EXPECT_EQ(registry.exposition(), "# HELP bacuri_test_seconds Test latency.\n"
                                     "# TYPE bacuri_test_seconds histogram\n"
                                     "bacuri_test_seconds_bucket{kind=\"live\",le=\"0.1\"} 1\n"
                                     "bacuri_test_seconds_bucket{kind=\"live\",le=\"0.5\"} 3\n"
                                     "bacuri_test_seconds_bucket{kind=\"live\",le=\"+Inf\"} 4\n"
                                     "bacuri_test_seconds_sum{kind=\"live\"} 2.8125\n"
                                     "bacuri_test_seconds_count{kind=\"live\"} 4\n");
}

TEST(Metrics, CounterSeriesAreSharedAndLabelValuesEscaped)
{
    Registry registry;
    registry.counter({.name = "bacuri_test_total", .help = "Test events."}, {{"reason", "a\"b\\c\nd"}})
        .increment();
    registry.counter({.name = "bacuri_test_total", .help = "Test events."}, {{"reason", "a\"b\\c\nd"}})
        .increment(2);
    EXPECT_EQ(registry.exposition(), "# HELP bacuri_test_total Test events.\n"
                                     "# TYPE bacuri_test_total counter\n"
                                     "bacuri_test_total{reason=\"a\\\"b\\\\c\\nd\"} 3\n");
    EXPECT_THROW(registry.histogram({.name = "bacuri_test_total", .help = "Other type."}, interaction_buckets),
                 std::logic_error);
}

TEST(Metrics, ExporterReplacesTheFileAndWritesOnStop)
{
    const auto file =
        std::filesystem::temp_directory_path() / ("bacuri-metrics-" + std::to_string(std::random_device{}()));
    Registry registry;
    auto& events = registry.counter({.name = "bacuri_test_total", .help = "Test events."});
    {
        Exporter exporter{registry, file, std::chrono::seconds{3600}};
        events.increment();
    }
    std::ifstream exported{file};
    const std::string text{std::istreambuf_iterator<char>{exported}, {}};
    EXPECT_NE(text.find("bacuri_test_total 1\n"), std::string::npos) << text;
    EXPECT_FALSE(std::filesystem::exists(file.string() + ".next"));
    std::filesystem::remove(file);
}
} // namespace
} // namespace bacuri::vms::observability
