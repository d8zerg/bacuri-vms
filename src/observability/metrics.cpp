#include "observability/metrics.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <ranges>
#include <stdexcept>

namespace bacuri::vms::observability
{
namespace
{
// Label values escape the backslash, the double quote and the line feed.
std::string escaped(std::string_view value)
{
    std::string result;
    for (const char c : value) {
        switch (c) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        default:
            result += c;
        }
    }
    return result;
}

void export_once(const Exporter& exporter) noexcept
{
    try {
        exporter.write();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "metrics export unavailable: %s\n", error.what());
    }
}

std::string series(const std::string& name, const Labels& labels, const std::string& extra = {})
{
    std::string text = name;
    if (labels.empty() && extra.empty())
        return text;
    text += '{';
    bool first = true;
    for (const auto& [key, value] : labels) {
        text += (first ? "" : ",") + key + "=\"" + escaped(value) + '"';
        first = false;
    }
    if (!extra.empty())
        text += (first ? "" : ",") + extra;
    return text + '}';
}

// The shortest text that reads back as the same double.
std::string number(double value)
{
    std::array<char, 32> text{};
    const auto [end, error] = std::to_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} ? std::string(text.data(), end) : std::string{"NaN"};
}
} // namespace

Histogram::Histogram(std::span<const double> bounds)
    : bounds_{bounds.begin(), bounds.end()},
      buckets_{std::make_unique<std::atomic<std::uint64_t>[]>(bounds.size() + 1)}
{
    if (!std::ranges::is_sorted(bounds_) || std::ranges::adjacent_find(bounds_) != bounds_.end())
        throw std::invalid_argument("Histogram bounds must increase");
}

void Histogram::observe(std::chrono::duration<double> value) noexcept
{
    const auto seconds = value.count();
    const auto bucket = std::ranges::lower_bound(bounds_, seconds) - bounds_.begin();
    buckets_[static_cast<std::size_t>(bucket)].fetch_add(1, std::memory_order_relaxed);
    sum_.fetch_add(seconds, std::memory_order_relaxed);
    count_.fetch_add(1, std::memory_order_relaxed);
}

Histogram::Snapshot Histogram::snapshot() const
{
    Snapshot result{{}, 0, sum_.load(std::memory_order_relaxed)};
    std::uint64_t total = 0;
    for (std::size_t i = 0; i <= bounds_.size(); ++i) {
        total += buckets_[i].load(std::memory_order_relaxed);
        result.cumulative.push_back(total);
    }
    // The count equals the +Inf bucket, so the exposition stays consistent under concurrent updates.
    result.count = total;
    return result;
}

Registry& Registry::process()
{
    static Registry registry;
    return registry;
}

Counter& Registry::counter(const Metric& metric, const Labels& labels)
{
    const auto& [name, help] = metric;
    std::scoped_lock guard{mutex_};
    auto& family = families_[name];
    if (family.type.empty())
        family = {help, "counter", {}, {}, {}};
    if (family.type != "counter")
        throw std::logic_error("Metric " + name + " has another type");
    auto& counter = family.counters[labels];
    if (!counter)
        counter = std::make_unique<Counter>();
    return *counter;
}

void Registry::set_gauges(const Metric& metric, std::map<Labels, double> values)
{
    std::scoped_lock guard{mutex_};
    auto& family = families_[metric.name];
    if (family.type.empty())
        family = {metric.help, "gauge", {}, {}, {}};
    if (family.type != "gauge")
        throw std::logic_error("Metric " + metric.name + " has another type");
    family.gauges = std::move(values);
}

Histogram& Registry::histogram(const Metric& metric, std::span<const double> bounds, const Labels& labels)
{
    const auto& [name, help] = metric;
    std::scoped_lock guard{mutex_};
    auto& family = families_[name];
    if (family.type.empty())
        family = {help, "histogram", {}, {}, {}};
    if (family.type != "histogram")
        throw std::logic_error("Metric " + name + " has another type");
    auto& histogram = family.histograms[labels];
    if (!histogram)
        histogram = std::make_unique<Histogram>(bounds);
    return *histogram;
}

std::string Registry::exposition() const
{
    std::scoped_lock guard{mutex_};
    std::string text;
    for (const auto& [name, family] : families_) {
        text.append("# HELP ").append(name).append(" ").append(family.help);
        text.append("\n# TYPE ").append(name).append(" ").append(family.type).append("\n");
        for (const auto& [labels, gauge] : family.gauges)
            text += series(name, labels) + ' ' + number(gauge) + '\n';
        for (const auto& [labels, counter] : family.counters)
            text += series(name, labels) + ' ' + number(counter->value()) + '\n';
        for (const auto& [labels, histogram] : family.histograms) {
            const auto snapshot = histogram->snapshot();
            const auto bounds = histogram->bounds();
            for (std::size_t i = 0; i < snapshot.cumulative.size(); ++i) {
                const auto le = i < bounds.size() ? number(bounds[i]) : std::string{"+Inf"};
                text += series(name + "_bucket", labels, "le=\"" + le + '"') + ' ' +
                        std::to_string(snapshot.cumulative[i]) + '\n';
            }
            text += series(name + "_sum", labels) + ' ' + number(snapshot.sum) + '\n';
            text += series(name + "_count", labels) + ' ' + std::to_string(snapshot.count) + '\n';
        }
    }
    return text;
}

Exporter::Exporter(const Registry& registry, std::filesystem::path file, std::chrono::seconds interval)
    : registry_{registry}, file_{std::move(file)}, worker_{[this, interval](const std::stop_token& stop) {
          std::mutex mutex;
          std::condition_variable_any wake;
          std::unique_lock guard{mutex};
          while (!stop.stop_requested()) {
              export_once(*this);
              wake.wait_for(guard, stop, interval, [] { return false; });
          }
          // The last exposition includes the observations made before the process stopped.
          export_once(*this);
      }}
{
}

std::unique_ptr<Exporter> export_process_metrics(const char* directory, std::string_view process)
{
    if (!directory || !*directory)
        return {};
    return std::make_unique<Exporter>(Registry::process(),
                                      std::filesystem::path{directory} / (std::string{process} + ".prom"));
}

void Exporter::write() const
{
    auto next = file_;
    next += ".next";
    {
        std::ofstream output{next, std::ios::binary | std::ios::trunc};
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output << registry_.exposition();
    }
    std::filesystem::rename(next, file_);
}
} // namespace bacuri::vms::observability
