#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace bacuri::vms::observability
{
// Label names and values of one time series, ordered by name.
using Labels = std::map<std::string, std::string>;

class Counter
{
  public:
    void increment(double value = 1) noexcept
    {
        value_.fetch_add(value, std::memory_order_relaxed);
    }
    [[nodiscard]] double value() const noexcept
    {
        return value_.load(std::memory_order_relaxed);
    }

  private:
    std::atomic<double> value_{};
};

// Cumulative histogram of durations in seconds with fixed upper bounds.
class Histogram
{
  public:
    explicit Histogram(std::span<const double> bounds);
    void observe(std::chrono::duration<double> value) noexcept;

    struct Snapshot {
        std::vector<std::uint64_t> cumulative;
        std::uint64_t count;
        double sum;
    };
    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] std::span<const double> bounds() const noexcept
    {
        return bounds_;
    }

  private:
    std::vector<double> bounds_;
    std::unique_ptr<std::atomic<std::uint64_t>[]> buckets_;
    std::atomic<std::uint64_t> count_{};
    std::atomic<double> sum_{};
};

// Bucket bounds for operator waits: first frames, alarms and interactive requests.
inline constexpr double interaction_buckets[] = {0.05, 0.1, 0.2, 0.3, 0.5, 0.75, 1, 1.5, 2, 3, 5, 10};
// Bucket bounds for archive work that follows the keyframe interval of a camera.
inline constexpr double archive_buckets[] = {0.25, 0.5, 1, 2, 4, 8, 15, 30, 60, 120};

// Name and help text of a metric family. Named fields keep the two strings from being swapped.
struct Metric {
    std::string name;
    std::string help;
};

// Metrics of one process in the Prometheus text exposition format. Counters and histograms are cumulative.
class Registry
{
  public:
    static Registry& process();
    Counter& counter(const Metric& metric, const Labels& labels = {});
    // Replace every series in this gauge family, dropping labels absent from this snapshot.
    void set_gauges(const Metric& metric, std::map<Labels, double> values);
    Histogram& histogram(const Metric& metric, std::span<const double> bounds, const Labels& labels = {});
    [[nodiscard]] std::string exposition() const;

  private:
    struct Family {
        std::string help, type;
        std::map<Labels, std::unique_ptr<Counter>> counters;
        std::map<Labels, std::unique_ptr<Histogram>> histograms;
        std::map<Labels, double> gauges;
    };
    mutable std::mutex mutex_;
    std::map<std::string, Family> families_;
};

// Writes the exposition of the registry to a file every interval and on stop. The file is replaced
// atomically, so a reader never sees a partial exposition.
class Exporter
{
  public:
    Exporter(const Registry& registry, std::filesystem::path file,
             std::chrono::seconds interval = std::chrono::seconds{10});
    void write() const;

  private:
    const Registry& registry_;
    std::filesystem::path file_;
    std::jthread worker_;
};

// Exports the registry of the process to DIRECTORY/PROCESS.prom for the lifetime of the result.
// Returns nothing when no directory is configured.
std::unique_ptr<Exporter> export_process_metrics(const char* directory, std::string_view process);
} // namespace bacuri::vms::observability
