#include "media/acceleration/settings.hpp"
#include "media/acceleration/budget.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <map>

namespace bacuri::vms::acceleration
{
namespace
{
using Json = nlohmann::json;
struct Samples {
    std::map<std::pair<std::string, std::string>, const Json*> measured;

    const Json* find(const char* operation, const char* codec) const
    {
        auto it = measured.find({operation, codec});
        return it == measured.end() ? nullptr : it->second;
    }
    std::optional<Cost> cost(const Task& task, std::uint64_t memory) const
    {
        Cost out{0, task.encoder ? 1u : 0u};
        for (const auto* codec : {"h264", "h265"}) {
            const auto* sample = find(task.operation, codec);
            const auto* decoder = find("decode", codec);
            if (!sample || !decoder || (task.encoder && !find("encode", "h264")))
                return {};
            auto fps = std::min(sample->value("fps", 0.), decoder->value("fps", 0.));
            if (task.encoder)
                fps = std::min(fps, find("encode", "h264")->value("fps", 0.));
            if (!(fps > 0.))
                return {};
            const double share = std::ceil(1'000'000. * task.rate / fps);
            out.time = std::max(out.time, static_cast<unsigned>(std::min(share, 1'000'001.)));
            const auto bytes = sample->value("sampled_device_memory_bytes", std::uint64_t{});
            out.memory = std::max(out.memory, bytes);
            out.exclusive |= bytes == 0 || memory == 0;
        }
        return out;
    }
};
} // namespace
bool Device::fits(const Cost& reserved, const Cost& cost) const
{
    return reserved.time <= 1'000'000 && reserved.encoders <= sessions && (!memory || reserved.memory <= memory) &&
           cost.time <= 1'000'000 - reserved.time && cost.encoders <= sessions - reserved.encoders &&
           !(reserved.time && (reserved.exclusive || cost.exclusive)) &&
           (!memory || cost.memory <= memory - reserved.memory);
}
void reserve(Cost& used, const Cost& cost)
{
    used.time += cost.time;
    used.encoders += cost.encoders;
    used.memory += cost.memory;
    used.exclusive |= cost.exclusive;
}
std::vector<Device> budgets(const Json& probe)
{
    std::vector<Device> devices;
    const auto& result = probe.at("result");
    if (result.is_null() || probe.at("stale") == true)
        return devices;
    for (const auto& value : result.at("devices")) {
        if (value.at("backend") == "cpu")
            continue;
        Device device{};
        device.id = value.at("id");
        if (!value.at("memory_bytes").is_null())
            device.memory = value.at("memory_bytes");
        Samples samples;
        for (const auto& m : result.at("measurements"))
            if (m.at("device_id") == device.id && m.at("state") == "measured")
                samples.measured.emplace(
                    std::make_pair(m.at("operation").get<std::string>(), m.at("codec").get<std::string>()), &m);
        if (const auto* s = samples.find("sessions", "h264"))
            device.sessions = s->value("verified_sessions", 0u);
        std::ranges::transform(tasks, device.costs.begin(),
                               [&](const Task& task) { return samples.cost(task, device.memory); });
        devices.push_back(std::move(device));
    }
    std::ranges::sort(devices, {}, &Device::id);
    return devices;
}
namespace
{
// Higher bits are higher-priority tasks. Seven task kinds bound the search independently of the GPU count.
constexpr unsigned combinations = 1u << tasks.size();
constexpr unsigned bit(std::size_t task)
{
    return (combinations / 2) >> task;
}
using Assignment = std::array<std::size_t, tasks.size()>;
struct Placement {
    unsigned mask{};
    Assignment assignment{};
};
Placement place(const std::vector<Device>& devices, unsigned requested)
{
    struct Option {
        Assignment assignment{};
        unsigned time{};
    };
    std::array<std::optional<Option>, combinations> reachable{};
    reachable[0] = Option{};
    for (std::size_t d = 0; d < devices.size(); ++d) {
        const auto& device = devices[d];
        // Combine sets that fit this GPU with sets already placed on earlier GPUs.
        const auto previous = reachable;
        for (unsigned subset = requested; subset; subset = (subset - 1) & requested) {
            Cost usage{};
            bool fits = true;
            for (std::size_t t = 0; t < tasks.size(); ++t) {
                if (!(subset & bit(t)))
                    continue;
                const auto& cost = device.costs[t];
                if (!cost || !device.fits(usage, *cost)) {
                    fits = false;
                    break;
                }
                reserve(usage, *cost);
            }
            if (!fits)
                continue;
            for (unsigned prior = 0; prior < combinations; ++prior) {
                if (!previous[prior] || (prior & subset))
                    continue;
                const auto time = previous[prior]->time + usage.time;
                const auto& current = reachable[prior | subset];
                if (current && current->time <= time)
                    continue;
                auto assignment = previous[prior]->assignment;
                for (std::size_t t = 0; t < tasks.size(); ++t)
                    if (subset & bit(t))
                        assignment[t] = d;
                reachable[prior | subset] = Option{assignment, time};
            }
        }
    }
    for (unsigned mask = requested; mask > 0; --mask)
        if (reachable[mask])
            return {mask, reachable[mask]->assignment};
    return {};
}
unsigned requested_tasks(const Settings& settings)
{
    unsigned requested{};
    for (std::size_t t = 0; t < tasks.size(); ++t)
        if (settings.mode == "auto" ||
            std::ranges::find(settings.gpu_tasks, tasks[t].name) != settings.gpu_tasks.end())
            requested |= bit(t);
    return requested;
}
} // namespace
std::array<std::string, tasks.size()> preferred_devices(const Settings& settings,
                                                        const std::vector<Device>& devices)
{
    const auto placement = place(devices, requested_tasks(settings));
    std::array<std::string, tasks.size()> preferred;
    for (std::size_t t = 0; t < tasks.size(); ++t)
        if (placement.mask & bit(t))
            preferred[t] = devices[placement.assignment[t]].id;
    return preferred;
}
bool normalize(Settings& settings)
{
    if (settings.mode != "manual")
        return (settings.mode == "auto" || settings.mode == "cpu_only") && settings.gpu_tasks.empty();
    std::vector<std::string> ordered;
    for (const auto& task : tasks)
        if (std::ranges::find(settings.gpu_tasks, task.name) != settings.gpu_tasks.end())
            ordered.emplace_back(task.name);
    if (ordered.size() != settings.gpu_tasks.size())
        return false;
    settings.gpu_tasks = std::move(ordered);
    return true;
}
nlohmann::json evaluate(const Settings& settings, const nlohmann::json& probe)
{
    auto devices = budgets(probe);
    const auto requested = requested_tasks(settings);
    const auto placement = place(devices, requested);
    Json decisions = Json::array(), rejected = Json::array();
    for (std::size_t t = 0; t < tasks.size(); ++t) {
        Json row = {{"task", tasks[t].name},
                    {"device_id", "cpu"},
                    {"reason", settings.mode == "cpu_only" ? "cpu_only" : "not_selected"}};
        if (placement.mask & bit(t)) {
            auto& device = devices[placement.assignment[t]];
            const auto& measured = device.costs[t];
            if (!measured)
                throw std::logic_error("Missing cost for an assigned task");
            const auto& cost = *measured;
            reserve(device.used, cost);
            row.update(Json{{"device_id", device.id},
                            {"reason", "within_budget"},
                            {"time_ppm", cost.time},
                            {"encoder_sessions", cost.encoders},
                            {"memory_bytes", cost.memory},
                            {"exclusive", cost.exclusive}});
        } else if (requested & bit(t)) {
            row["reason"] = std::ranges::any_of(devices, [&](const Device& d) { return d.costs[t].has_value(); })
                                ? "budget_exceeded"
                                : "unsupported";
            if (settings.mode == "manual")
                rejected.push_back(tasks[t].name);
        }
        decisions.push_back(std::move(row));
    }
    Json limits = Json::array();
    for (const auto& device : devices) {
        Json costs = Json::array();
        for (std::size_t t = 0; t < tasks.size(); ++t)
            if (const auto& cost = device.costs[t])
                costs.push_back({{"task", tasks[t].name},
                                 {"time_ppm", cost->time},
                                 {"encoder_sessions", cost->encoders},
                                 {"memory_bytes", cost->memory},
                                 {"exclusive", cost->exclusive}});
        limits.push_back({{"device_id", device.id},
                          {"time_ppm_used", device.used.time},
                          {"encoder_sessions", device.sessions},
                          {"encoder_sessions_used", device.used.encoders},
                          {"memory_bytes", device.memory},
                          {"memory_bytes_used", device.used.memory},
                          {"costs", std::move(costs)}});
    }
    return {{"decisions", std::move(decisions)},
            {"devices", std::move(limits)},
            {"rejected_tasks", std::move(rejected)}};
}
} // namespace bacuri::vms::acceleration
