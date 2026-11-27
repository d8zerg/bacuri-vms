#include "media/acceleration/probe.hpp"
#include <glib.h>
#include <fstream>

namespace bacuri::vms::acceleration
{
namespace
{
using namespace std::chrono_literals;
constexpr std::array stages{"inventory", "decode", "encode", "inference", "workloads"};
std::string timestamp()
{
    auto* now = g_date_time_new_now_utc();
    auto* value = g_date_time_format_iso8601(now);
    std::string result{value};
    g_free(value);
    g_date_time_unref(now);
    return result;
}
std::string identifier()
{
    auto* value = g_uuid_string_random();
    std::string result{value};
    g_free(value);
    return result;
}
std::vector<Json> samples(std::string_view stage, const Json& devices)
{
    std::vector<Json> result;
    if (stage == "inventory") {
        result.push_back({{"stage", stage}});
        return result;
    }
    for (const auto& device : devices) {
        auto add = [&](const char* op, const char* codec) {
            result.push_back({{"stage", stage}, {"device", device}, {"codec", codec}, {"operation", op}});
        };
        if (stage == "inference") {
            if (device.at("backend") == "cpu") {
                add("cpu", "");
                add("openvino", "");
            }
            if (device.at("backend") == "nvidia")
                add("cuda", "");
        } else
            for (const auto* codec : {"h264", "h265"}) {
                if (stage == "decode")
                    add("decode", codec);
                if (stage == "encode") {
                    add("encode", codec);
                    if (device.at("backend") != "cpu")
                        add("sessions", codec);
                }
                if (stage == "workloads")
                    for (const auto* op : {"mask", "profile", "export", "keyframe", "thumbnail", "analysis"})
                        add(op, codec);
            }
    }
    return result;
}
} // namespace
Probe::Probe(std::string node, const std::filesystem::path& directory, std::atomic_uint& activity, Run run,
             std::function<std::string()> identify, std::chrono::milliseconds watch)
    : node_{std::move(node)}, file_{directory / ("probe-" + node_ + ".json")}, activity_{activity},
      run_{std::move(run)}, identify_{std::move(identify)}, watch_{watch}
{
    std::ifstream input{file_};
    if (input) {
        auto data = Json::parse(input, nullptr, false);
        if (data.is_object() && data.contains("node_id") && data.at("node_id") == node_ &&
            data.contains("fingerprint") && data.contains("devices") && data.contains("measurements") &&
            data.contains("measured_at"))
            result_ = std::move(data);
    }
    identity_ = identify_();
    schedule("startup");
    thread_ = std::jthread{[this](const std::stop_token& stop) { loop(stop); }};
}
Probe::~Probe()
{
    thread_.request_stop();
    changed_.notify_all();
}
void Probe::schedule(std::string_view reason)
{
    job_ = {{"id", identifier()}, {"state", "queued"}, {"reason", reason}, {"steps", Json::array()}};
    for (const auto* name : stages)
        job_["steps"].push_back({{"name", name}, {"state", "pending"}, {"completed", 0}, {"total", 0}});
    pending_ = true;
    changed_.notify_all();
}
Json Probe::snapshot() const
{
    return {{"node_id", node_},
            {"job", job_},
            {"result", result_},
            {"stale", !result_.is_null() && result_.at("fingerprint") != identity_}};
}
Json Probe::status()
{
    std::scoped_lock lock{mutex_};
    return snapshot();
}
Json Probe::start(bool prepare_gpu)
{
    std::scoped_lock lock{mutex_};
    const auto& state = job_.at("state");
    if (state != "running" && state != "queued" && state != "waiting")
        schedule(prepare_gpu ? "prepare_gpu" : "administrator");
    return snapshot();
}
bool Probe::cancel(const std::string& id, Json& reply)
{
    std::scoped_lock lock{mutex_};
    if (job_.at("id") != id)
        return false;
    auto& state = job_["state"];
    if (state == "queued" || state == "running" || state == "waiting") {
        state = "cancelled";
        pending_ = false;
        for (auto& step : job_["steps"])
            if (step["state"] == "running")
                step["state"] = "cancelled";
        changed_.notify_all();
    }
    reply = snapshot();
    return true;
}
void Probe::execute(const std::stop_token& stop, const std::string& id)
{
    const auto identity = identity_;
    auto obsolete = [&] { return stop.stop_requested() || job_["id"] != id || job_["state"] == "cancelled"; };
    auto next_identity_check = std::chrono::steady_clock::now() + watch_;
    Json devices = Json::array(), measurements = Json::array();
    for (std::size_t i = 0; i < stages.size(); ++i) {
        auto tasks = samples(stages[i], devices);
        for (const auto& task : tasks) {
            for (;;) {
                {
                    std::unique_lock lock{mutex_};
                    if (obsolete())
                        return;
                    if (std::chrono::steady_clock::now() >= next_identity_check) {
                        auto current = identify_();
                        if (current != identity) {
                            identity_ = std::move(current);
                            schedule("device_changed");
                            return;
                        }
                        next_identity_check = std::chrono::steady_clock::now() + watch_;
                    }
                    if (activity_.load() > 0) {
                        job_["state"] = "waiting";
                        changed_.wait_for(lock, 250ms);
                        continue;
                    }
                    job_["state"] = "running";
                    auto& step = job_["steps"][i];
                    step["state"] = "running";
                    step["total"] = tasks.size();
                }
                bool yielded = false;
                auto value = run_(task, [&] {
                    std::scoped_lock lock{mutex_};
                    yielded = activity_.load() > 0;
                    return obsolete() || yielded;
                });
                std::unique_lock lock{mutex_};
                if (obsolete())
                    return;
                if (yielded || value.value("state", "") == "busy") {
                    job_["state"] = "waiting";
                    changed_.wait_for(lock, 500ms);
                    continue;
                }
                if (value.at("state") == "interrupted_or_timeout")
                    value["state"] = "timeout";
                if (i == 0) {
                    if (value.at("state") != "measured" || value.at("devices").empty())
                        throw std::runtime_error("Inventory failed");
                    devices = value.at("devices");
                } else {
                    if (task.at("device").at("backend") == "cpu" && task.at("operation") != "openvino" &&
                        value.at("state") != "measured")
                        throw std::runtime_error("Required CPU reference failed");
                    value["device_id"] = task.at("device").at("id");
                    value["operation"] = task.at("operation");
                    value["codec"] = task.at("codec");
                    if (value.contains("fps") && task.at("operation") != "thumbnail" &&
                        task.at("operation") != "keyframe")
                        value["verified_25fps_streams"] = value.at("fps").get<double>() >= 25. ? 1 : 0;
                    measurements.push_back(std::move(value));
                }
                auto& step = job_["steps"][i];
                step["completed"] = step.at("completed").get<unsigned>() + 1;
                break;
            }
        }
        std::scoped_lock lock{mutex_};
        if (obsolete())
            return;
        job_["steps"][i]["state"] = "completed";
    }
    auto final_identity = identify_();
    std::scoped_lock lock{mutex_};
    if (obsolete())
        return;
    if (final_identity != identity) {
        identity_ = std::move(final_identity);
        schedule("device_changed");
        return;
    }
    Json result = {{"node_id", node_},
                   {"fingerprint", identity},
                   {"measured_at", timestamp()},
                   {"devices", std::move(devices)},
                   {"measurements", std::move(measurements)}};
    auto data = result.dump();
    if (!g_file_set_contents_full(
            file_.c_str(), data.c_str(), static_cast<gssize>(data.size()),
            // GLib declares these combinable flags as C enum constants.
            // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
            static_cast<GFileSetContentsFlags>(G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE), 0600,
            nullptr))
        throw std::runtime_error("Cannot persist probe");
    result_ = std::move(result);
    job_["state"] = "completed";
}
void Probe::loop(const std::stop_token& stop)
{
    std::unique_lock lock{mutex_};
    while (!stop.stop_requested()) {
        if (!pending_) {
            changed_.wait_for(lock, stop, watch_, [&] { return pending_; });
            if (stop.stop_requested())
                break;
            if (!pending_) {
                auto current = identify_();
                if (current != identity_) {
                    identity_ = std::move(current);
                    schedule("device_changed");
                }
                continue;
            }
        }
        auto id = job_.at("id").get<std::string>();
        pending_ = false;
        lock.unlock();
        try {
            execute(stop, id);
        } catch (const std::exception&) {
            std::scoped_lock guard{mutex_};
            if (job_["id"] == id && job_["state"] != "cancelled") {
                job_["state"] = "failed";
                for (auto& step : job_["steps"])
                    if (step["state"] == "running")
                        step["state"] = "failed";
            }
        }
        lock.lock();
    }
}
} // namespace bacuri::vms::acceleration
