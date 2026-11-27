#include "media/acceleration/scheduler.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <tuple>
#include <glib.h>

namespace bacuri::vms::acceleration
{
namespace
{
std::string token()
{
    char* value = g_uuid_string_random();
    std::string result{value};
    g_free(value);
    return result;
}
const char* failure_reason(Report report)
{
    switch (report) {
    case Report::session_limit:
        return "session_limit";
    case Report::device_lost:
        return "device_lost";
    case Report::driver_reset:
        return "driver_reset";
    case Report::encoder_error:
        return "encoder_error";
    default:
        throw std::invalid_argument("Invalid device failure");
    }
}
} // namespace
Scheduler::Scheduler(Notify notify, Instant start) : notify_{std::move(notify)}, startup_{start + lease} {}
void Scheduler::configure(Settings policy, std::vector<Device> devices, bool stale)
{
    if (devices.size() > 1 && policy.mode == "manual" && (devices_ != devices || policy_ != policy))
        preferred_ = preferred_devices(policy, devices);
    else if (devices.size() <= 1 || policy.mode != "manual")
        preferred_ = {};
    policy_ = std::move(policy);
    devices_ = std::move(devices);
    stale_ = stale;
}
void Scheduler::move(const std::string& id, Job& job, const std::string& target, std::string reason)
{
    auto& assignment = job.assignment;
    if (target != assignment.device || reason != assignment.reason) {
        notify_({id, tasks[job.kind].name, assignment.device, target, reason, job.held});
        assignment.device = target;
        assignment.reason = std::move(reason);
    }
}
void Scheduler::fault(const Job& job, Report report, Instant now)
{
    auto& entry = faults_[job.held];
    // A late session error cannot downgrade a device-wide quarantine.
    if (!entry.reason.empty() && !entry.sessions && report == Report::session_limit)
        return;
    std::optional<unsigned> capacity;
    if (report == Report::session_limit) {
        unsigned others{};
        for (const auto& [id, other] : jobs_)
            if (&other != &job && other.held == job.held)
                others += other.charge.encoders;
        capacity = std::min(others, entry.sessions.value_or(others));
    }
    const auto reason = failure_reason(report);
    if (entry.reason != reason || capacity != entry.sessions)
        notify_({{}, {}, entry.reason.empty() ? "available" : "degraded", "degraded", reason, job.held});
    entry.reason = reason;
    entry.sessions = capacity;
    entry.kinds.insert(job.kind);
    entry.generation = ++sequence_;
    entry.retry = now + cooldown;
    entry.checking = false;
}
Assignment Scheduler::update(const TaskUpdate& request, Instant now)
{
    expire(now);
    auto it = jobs_.find(request.id);
    if (request.report == Report::finished && it == jobs_.end())
        return {};
    if (request.kind >= tasks.size() || (it != jobs_.end() && it->second.kind != request.kind) ||
        (request.report == Report::poll && !request.released.empty()))
        throw std::invalid_argument("Invalid task update");
    if (it == jobs_.end()) {
        if (request.report != Report::poll)
            throw std::invalid_argument("Unknown task");
        if (jobs_.size() >= 4096)
            throw std::length_error("Node task limit");
        Job pending{};
        pending.kind = request.kind;
        pending.order = ++sequence_;
        it = jobs_.emplace(request.id, std::move(pending)).first;
    }
    auto& job = it->second;
    if (request.report == Report::poll || request.released == job.assignment.grant)
        job.cpu_until_checkpoint = request.cpu_until_checkpoint;
    if (request.running == "cpu" ||
        (!job.held.empty() && request.running == job.held && request.running_grant == job.assignment.grant))
        job.running = request.running;
    if (request.report != Report::poll) {
        if (request.report == Report::finished && request.released == job.assignment.grant) {
            move(it->first, job, "finished", "finished");
            jobs_.erase(it);
            plan(now);
            return {};
        }
        // A replay must never release or quarantine a subsequent reservation.
        if (!request.released.empty() && request.released == job.assignment.grant) {
            if (request.report != Report::released && request.report != Report::finished) {
                if (request.report == Report::session_limit && !job.charge.encoders)
                    throw std::invalid_argument("Task has no encoder");
                fault(job, request.report, now);
                move(it->first, job, "cpu", failure_reason(request.report));
                // An encoder that never opened may retry after a lower priority holder releases.
                if (request.report != Report::session_limit)
                    job.promote = now + cooldown;
            } else if (job.assignment.device == job.held) {
                move(it->first, job, "cpu", "released");
                job.promote = now + cooldown;
            }
            job.held.clear();
            job.running.clear();
            job.assignment.grant.clear();
            job.charge = {};
        }
    }
    job.wait = request.wait;
    job.expires = now + lease;
    plan(now, &job);
    if (!job.held.empty() && job.assignment.device == job.held)
        job.grant_until = now + lease;
    return job.assignment;
}
void Scheduler::expire(Instant now)
{
    std::erase_if(jobs_, [&](auto& pair) {
        const auto& [id, job] = pair;
        if (job.expires > now)
            return false;
        notify_({id, tasks[job.kind].name, job.assignment.device, "finished", "expired", job.held});
        return true;
    });
    for (auto& [id, job] : jobs_)
        if (!job.held.empty() && job.assignment.device != job.held && now >= job.grant_until) {
            if (job.running == job.held)
                job.running.clear();
            job.held.clear();
            job.assignment.grant.clear();
            job.charge = {};
        }
}
void Scheduler::tick(Instant now)
{
    expire(now);
    plan(now);
}
void Scheduler::plan(Instant now, const Job* polling)
{
    using Entry = std::map<std::string, Job>::value_type;
    std::vector<Entry*> ordered;
    ordered.reserve(jobs_.size());
    for (auto& entry : jobs_)
        ordered.push_back(&entry);
    std::ranges::sort(ordered, [](const Entry* a, const Entry* b) {
        return std::tie(a->second.kind, a->second.order) < std::tie(b->second.kind, b->second.order);
    });
    // Physical reservations include revoked grants. The priority plan excludes them.
    std::map<std::string, Cost> physical, desired;
    for (auto* entry : ordered) {
        auto& job = entry->second;
        if (job.held.empty())
            continue;
        const auto dev = std::ranges::find(devices_, job.held, &Device::id);
        if (dev != devices_.end() && dev->costs[job.kind] && job.assignment.device == job.held)
            job.charge = dev->costs[job.kind].value();
        reserve(physical[job.held], job.charge);
    }
    const auto available = [&](const Device& device, const Cost& cost) {
        const auto fault = faults_.find(device.id);
        const bool session_free =
            fault == faults_.end() || !cost.encoders ||
            (fault->second.sessions && physical[device.id].encoders + cost.encoders <= *fault->second.sessions);
        return session_free && device.fits(physical[device.id], cost);
    };
    for (auto* entry : ordered) {
        auto& [id, job] = *entry;
        const bool holding = !job.held.empty() && job.assignment.device == job.held;
        const std::string fallback = job.wait && job.kind >= 3 ? "queue" : "cpu";
        std::string reason = "unsupported";
        const Device* best{};
        bool best_free{};
        const bool selected =
            policy_.mode == "auto" ||
            std::ranges::find(policy_.gpu_tasks, tasks[job.kind].name) != policy_.gpu_tasks.end();
        if (policy_.mode == "cpu_only")
            reason = "cpu_only";
        else if (job.cpu_until_checkpoint)
            reason = "cpu_checkpoint";
        else if (!selected)
            reason = "not_selected";
        else if (stale_)
            reason = "device_changed";
        else if (!holding && !job.held.empty())
            reason = job.assignment.reason;
        else if (now < startup_ || now < job.promote)
            reason = "cooldown";
        else
            for (const auto& device : devices_) {
                const auto& cost = device.costs[job.kind];
                if (!cost)
                    continue;
                const auto f = faults_.find(device.id);
                if (f != faults_.end() &&
                    (!f->second.sessions || (f->second.checking && !holding) ||
                     (cost->encoders && desired[device.id].encoders + cost->encoders > *f->second.sessions))) {
                    reason = f->second.reason;
                    continue;
                }
                if (!device.fits(desired[device.id], *cost)) {
                    reason = "budget_exceeded";
                    continue;
                }
                // Keep an eligible running pipeline on its device. New tasks prefer smaller measured cost.
                if (holding && job.held == device.id) {
                    best = &device;
                    break;
                }
                const bool free = available(device, *cost);
                const auto rank = [&](const Device& candidate, bool idle) {
                    return std::tuple{!idle, candidate.id != preferred_[job.kind],
                                      candidate.costs[job.kind].value().time};
                };
                if (!best || rank(device, free) < rank(*best, best_free)) {
                    best = &device;
                    best_free = free;
                }
            }
        if (best && (holding && best->id != job.held))
            best = nullptr;
        if (best) {
            const auto& cost = best->costs.at(job.kind).value();
            reserve(desired[best->id], cost);
            if (holding)
                continue;
            if (best_free && polling == &job) {
                reserve(physical[best->id], cost);
                job.held = best->id;
                job.charge = cost;
                job.grant_until = job.expires;
                job.assignment.grant = token();
                move(id, job, best->id, "within_budget");
                continue;
            }
            reason = best_free ? "budget_available" : "awaiting_release";
        }
        if (holding) {
            job.promote = now + cooldown;
            if (reason == "budget_exceeded")
                reason = "preempted";
        }
        move(id, job, fallback, std::move(reason));
    }
}
std::optional<HealthCheck> Scheduler::check_due(Instant now)
{
    if (stale_ || policy_.mode == "cpu_only")
        return {};
    for (auto& [id, fault] : faults_) {
        if (now < fault.retry || std::ranges::none_of(devices_, [&](const Device& d) { return d.id == id; }))
            continue;
        unsigned held{}, encoders{};
        for (const auto& [task, job] : jobs_)
            if (job.held == id) {
                ++held;
                encoders += job.charge.encoders;
            }
        if (fault.checking || (held && (!fault.sessions || !encoders || encoders != *fault.sessions)))
            continue;
        fault.retry = now + cooldown;
        fault.checking = true;
        return HealthCheck{id, fault.kinds, fault.generation,
                           fault.sessions ? std::optional{encoders} : std::nullopt};
    }
    return {};
}
void Scheduler::checked(const HealthCheck& check, unsigned verified, Instant now)
{
    auto it = faults_.find(check.device);
    if (it == faults_.end() || it->second.generation != check.generation)
        return;
    it->second.checking = false;
    if (!verified || stale_) {
        notify_({{}, {}, "degraded", "degraded", "check_failed", check.device});
        return;
    }
    if (check.encoders_in_use) {
        unsigned occupied{};
        for (const auto& [task, job] : jobs_)
            if (job.held == check.device)
                occupied += job.charge.encoders;
        const auto capacity = std::min(occupied, *check.encoders_in_use) + verified;
        const auto dev = std::ranges::find(devices_, check.device, &Device::id);
        if (dev == devices_.end())
            return;
        if (capacity < dev->sessions) {
            it->second.sessions = capacity;
            plan(now);
            return;
        }
    }
    notify_({{}, {}, "degraded", "available", "check_passed", check.device});
    faults_.erase(it);
    plan(now);
}
nlohmann::json Scheduler::status() const
{
    using Json = nlohmann::json;
    Json warnings = Json::array(), devices = Json::array();
    std::map<std::string, unsigned> counts{{"gpu", 0}, {"cpu", 0}, {"queue", 0}};
    std::map<std::tuple<std::size_t, std::string, std::string, std::string, std::string>, unsigned> assignments;
    std::map<std::string, Cost> used;
    for (const auto& [id, job] : jobs_) {
        if (!job.held.empty())
            reserve(used[job.held], job.charge);
        ++assignments[{job.kind, job.assignment.device, job.assignment.reason, job.held, job.running}];
        ++counts[job.assignment.device == "cpu" || job.assignment.device == "queue" ? job.assignment.device
                                                                                    : "gpu"];
    }
    if (stale_ && policy_.mode != "cpu_only")
        warnings.push_back({{"device_id", ""}, {"reason", "device_changed"}});
    for (const auto& [id, fault] : faults_)
        warnings.push_back({{"device_id", id}, {"reason", fault.reason}});
    for (const auto& device : devices_)
        used.try_emplace(device.id);
    for (const auto& [id, cost] : used) {
        devices.push_back({{"device_id", id},
                           {"encoder_sessions_used", cost.encoders},
                           {"memory_bytes_used", cost.memory},
                           {"time_ppm_used", cost.time}});
    }
    Json paths = Json::array();
    for (const auto& [key, count] : assignments) {
        const auto& [kind, device, reason, held, running] = key;
        Json path{{"task", tasks[kind].name}, {"device_id", device}, {"reason", reason}, {"count", count}};
        if (!running.empty())
            path["running_device_id"] = running;
        if (!held.empty() && held != device)
            path["releasing_device_id"] = held;
        paths.push_back(std::move(path));
    }
    return {{"mode", policy_.mode},
            {"tasks", counts},
            {"devices", devices},
            {"warnings", warnings},
            {"assignments", paths}};
}
} // namespace bacuri::vms::acceleration
