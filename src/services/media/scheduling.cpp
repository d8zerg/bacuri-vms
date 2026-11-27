#include "services/media/scheduling.hpp"
#include "media/lease.hpp"
#include "media/acceleration/health.hpp"
#include "observability/metrics.hpp"
#include "platform/identifiers.hpp"
#include "platform/rpc_status.hpp"
#include <algorithm>
#include <cstdio>

namespace bacuri::vms::media
{
using namespace std::chrono_literals;
using acceleration::Json;
namespace
{
std::string path_label(const std::string& value)
{
    if (value.empty())
        return "new";
    if (value == "cpu" || value == "queue" || value == "finished" || value == "degraded" || value == "available")
        return value;
    return "gpu";
}
} // namespace
Scheduling::Scheduling(std::string node, acceleration::Probe& probe, std::atomic_uint& activity)
    : node_{std::move(node)}, probe_{probe}, activity_{activity},
      scheduler_{[this](const auto& transition) { event(transition); }, Clock::now()},
      monitor_{[this](const std::stop_token& stop) { monitor(stop); }},
      telemetry_{[this](const std::stop_token& stop) { sample_devices(stop); }}
{
}
void Scheduling::event(const acceleration::Transition& change)
{
    const auto now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    const bool warning = change.to == "degraded" || change.reason == "preempted" ||
                         change.reason == "budget_exceeded" || change.reason == "device_changed";
    Json row{
        {"node_id", node_},        {"time_ms", now.count()},       {"task_id", change.task},
        {"kind", change.kind},     {"previous_path", change.from}, {"next_path", change.to},
        {"reason", change.reason}, {"device_id", change.device},   {"severity", warning ? "warning" : "info"}};
    std::fprintf(stderr, "%s\n", row.dump().c_str());
    if (journal_.size() >= 64)
        journal_.pop_front();
    journal_.push_back(std::move(row));
    if (change.from == change.to)
        return;
    observability::Registry::process()
        .counter(
            {"bacuri_acceleration_transitions_total", "Acceleration decisions by task kind, path and reason."},
            {{"kind", change.kind.empty() ? "device" : change.kind},
             {"previous_path", path_label(change.from)},
             {"next_path", path_label(change.to)},
             {"reason", change.reason}})
        .increment();
}
void Scheduling::publish()
{
    const auto state = scheduler_.status();
    auto& registry = observability::Registry::process();
    using Series = std::map<observability::Labels, double>;
    Series counts, sessions, time, memory, warnings;
    for (const auto& [path, count] : state.at("tasks").items())
        counts[{{"path", path}}] = count.get<double>();
    bool occupied = false;
    for (const auto& dev : state.at("devices")) {
        const observability::Labels label{{"device", dev.at("device_id").get<std::string>()}};
        sessions[label] = dev.at("encoder_sessions_used").get<double>();
        memory[label] = dev.at("memory_bytes_used").get<double>();
        time[label] = dev.at("time_ppm_used").get<double>();
        occupied |= time[label] > 0;
    }
    if (occupied && !gpu_activity_)
        gpu_activity_.emplace(activity_);
    if (!occupied)
        gpu_activity_.reset();
    for (const auto& row : state.at("warnings"))
        warnings[{{"device", row.at("device_id")}, {"reason", row.at("reason")}}] = 1;
    registry.set_gauges({"bacuri_acceleration_tasks", "Scheduled tasks by assigned path."}, std::move(counts));
    registry.set_gauges({"bacuri_acceleration_encoder_sessions", "Reserved hardware encoder sessions."},
                        std::move(sessions));
    registry.set_gauges({"bacuri_acceleration_memory_bytes", "Reserved device memory in bytes."},
                        std::move(memory));
    registry.set_gauges({"bacuri_acceleration_time_ppm", "Reserved GPU time in parts per million."},
                        std::move(time));
    registry.set_gauges({"bacuri_acceleration_device_warning", "Active device warnings by reason."},
                        std::move(warnings));
}
void Scheduling::sample_devices(const std::stop_token& stop)
{
    std::mutex mutex;
    std::condition_variable_any wake;
    std::unique_lock guard{mutex};
    auto& registry = observability::Registry::process();
    while (!stop.stop_requested()) {
        std::map<observability::Labels, double> sessions, memory, busy;
        const auto snapshot = probe_.status();
        if (!snapshot.at("stale").get<bool>() && snapshot.at("result").is_object()) {
            for (const auto& dev : snapshot.at("result").at("devices")) {
                if (stop.stop_requested())
                    break;
                try {
                    if (dev.at("backend") == "cpu")
                        continue;
                    const auto values = acceleration::subprocess({{"stage", "telemetry"}, {"device", dev}},
                                                                 [&] { return stop.stop_requested(); });
                    if (values.contains("state"))
                        throw std::runtime_error("Device telemetry sample failed");
                    const auto id = dev.at("id").get<std::string>();
                    const observability::Labels label{{"device", id}};
                    if (values.contains("encoder_sessions"))
                        sessions[label] = values.at("encoder_sessions").get<double>();
                    if (values.contains("memory_bytes"))
                        memory[label] = values.at("memory_bytes").get<double>();
                    if (values.contains("busy"))
                        for (const auto& [engine, percent] : values.at("busy").items())
                            busy[{{"device", id}, {"engine", engine}}] = percent.get<double>() / 100.;
                } catch (const std::exception&) {
                    registry
                        .counter(
                            {"bacuri_acceleration_telemetry_errors_total", "Failed device telemetry samples."})
                        .increment();
                }
            }
        }
        registry.set_gauges(
            {"bacuri_acceleration_observed_encoder_sessions", "Open hardware encoder sessions on the device."},
            std::move(sessions));
        registry.set_gauges({"bacuri_acceleration_observed_memory_bytes", "Device-wide video memory in use."},
                            std::move(memory));
        registry.set_gauges(
            {"bacuri_acceleration_engine_busy_ratio", "Observed fraction of time a device engine was busy."},
            std::move(busy));
        wake.wait_for(guard, stop, 10s, [] { return false; });
    }
}
grpc::Status Scheduling::update(const v1::TaskUpdate& request, v1::TaskAssignment& reply)
{
    if (request.node_id() != node_)
        return platform::rpc_status(ErrorCode::not_found);
    const auto kind = std::ranges::find(acceleration::tasks, request.task_kind(), &acceleration::Task::name);
    if (!platform::canonical_id(request.task_id()) || kind == acceleration::tasks.end() ||
        !v1::TaskUpdate_Report_IsValid(request.report()) ||
        (request.report() != v1::TaskUpdate::POLL && request.report() != v1::TaskUpdate::FINISHED &&
         request.released_grant().empty()) ||
        (!request.released_grant().empty() && !platform::canonical_id(request.released_grant())))
        return platform::rpc_status(ErrorCode::invalid_request);
    try {
        std::scoped_lock lock{mutex_};
        const auto assignment = scheduler_.update(
            {request.task_id(), static_cast<std::size_t>(kind - acceleration::tasks.begin()),
             request.wait_for_gpu(), static_cast<acceleration::Report>(request.report()), request.released_grant(),
             request.running_device_id(), request.running_grant(), request.cpu_until_checkpoint()},
            Clock::now());
        reply.set_device_id(assignment.device);
        reply.set_grant(assignment.grant);
        if (const auto it = factories_.find(assignment.device); it != factories_.end()) {
            reply.set_backend(it->second.at("backend").get<std::string>());
            for (const auto& [codec, factory] : it->second.at("codecs").items())
                (*reply.mutable_codecs())[codec] = factory.get<std::string>();
        }
        reply.set_reason(assignment.reason);
        reply.set_valid_for_ms(
            std::chrono::duration_cast<std::chrono::milliseconds>(acceleration::Scheduler::lease).count());
        publish();
        return grpc::Status::OK;
    } catch (const std::invalid_argument&) {
        return platform::rpc_status(ErrorCode::invalid_request);
    } catch (const std::length_error&) {
        return platform::rpc_status(ErrorCode::rate_limited);
    }
}
Json Scheduling::status()
{
    std::scoped_lock lock{mutex_};
    auto result = scheduler_.status();
    result["journal"] = journal_;
    return result;
}
void Scheduling::verify(acceleration::HealthCheck check, const Json& result)
{
    const auto& devices = result.at("devices");
    auto device = std::ranges::find_if(devices, [&](const Json& item) { return item.at("id") == check.device; });
    if (device == devices.end())
        throw std::logic_error("Missing device for recovery check");
    health_ = std::jthread{
        [this, check, device = *device, fingerprint = result.at("fingerprint")](const std::stop_token& stop) {
            acceleration::Activity foreground{activity_};
            unsigned verified{};
            try {
                verified = acceleration::verify_device(check, device, [&] { return stop.stop_requested(); });
                const auto current = probe_.status(true);
                if (current.at("stale") == true || current.at("result").is_null() ||
                    current.at("result").at("fingerprint") != fingerprint)
                    verified = 0;
            } catch (const std::exception&) {
                verified = 0;
            }
            std::scoped_lock lock{mutex_};
            checking_ = false;
            scheduler_.checked(check, verified, Clock::now());
            publish();
        }};
}
void Scheduling::monitor(const std::stop_token& stop)
{
    std::mutex wait_mutex;
    std::condition_variable_any wake;
    std::unique_lock wait_lock{wait_mutex};
    v1::NodeProbeRequest request;
    request.set_node_id(node_);
    std::string measured, identity;
    std::vector<acceleration::Device> devices;
    // A restart must requalify hardware, including a device quarantined by the previous process.
    bool startup_verified = false;
    const auto initial = probe_.status();
    const Json saved = initial.at("result").is_object() ? initial.at("result").at("measured_at") : Json(nullptr);
    std::unique_ptr<v1::MediaAuthorization::Stub> stub;
    while (!stop.stop_requested()) {
        std::optional<acceleration::HealthCheck> check;
        try {
            acceleration::Settings policy;
            if (!stub)
                stub = v1::MediaAuthorization::NewStub(local_channel("BACURI_AUTH_SOCKET"));
            grpc::ClientContext context;
            context.set_deadline(std::chrono::system_clock::now() + 750ms);
            v1::NodePolicy reply;
            if (stub->GetNodePolicy(&context, request, &reply).ok())
                policy = {reply.mode(), {reply.gpu_tasks().begin(), reply.gpu_tasks().end()}};
            const auto probe = probe_.status();
            const auto& result = probe.at("result");
            const auto stamp = result.is_object() ? result.at("measured_at").get<std::string>() : std::string{};
            startup_verified |= !stamp.empty() &&
                                (result.at("measured_at") != saved || probe.at("job").at("state") == "completed");
            const bool stale = probe.at("stale").get<bool>();
            if (stale || !startup_verified) {
                devices.clear();
                measured.clear();
            } else if (stamp != measured) {
                devices = acceleration::budgets(probe);
                measured = stamp;
            }
            {
                std::scoped_lock lock{mutex_};
                if (!result.is_null()) {
                    const auto fingerprint = result.at("fingerprint").get<std::string>();
                    if (!identity.empty() && identity != fingerprint) {
                        scheduler_.configure(policy, {}, true);
                        scheduler_.tick(Clock::now());
                    }
                    identity = fingerprint;
                }
                factories_.clear();
                if (!result.is_null() && !stale)
                    for (const auto& dev : result.at("devices"))
                        if (dev.at("backend") != "cpu")
                            factories_.emplace(dev.at("id").get<std::string>(), dev);
                scheduler_.configure(std::move(policy), devices, stale);
                scheduler_.tick(Clock::now());
                publish();
                if (!checking_) {
                    check = scheduler_.check_due(Clock::now());
                    checking_ = check.has_value();
                }
            }
            if (check)
                verify(*check, result);
        } catch (const std::exception&) {
            std::scoped_lock lock{mutex_};
            if (check) {
                checking_ = false;
                scheduler_.checked(*check, 0, Clock::now());
            }
            scheduler_.configure({}, {}, false);
            scheduler_.tick(Clock::now());
            publish();
        }
        wake.wait_for(wait_lock, stop, 1s, [] { return false; });
    }
}
} // namespace bacuri::vms::media
