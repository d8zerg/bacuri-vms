#pragma once
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>

namespace bacuri::vms::acceleration
{
using Json = nlohmann::json;
using Interrupt = std::function<bool()>;
using Run = std::function<Json(const Json&, const Interrupt&)>;
std::string hardware_identity();
Json inventory();
Json measure_video(const Json&);
Json measure_inference(const Json&);
Json subprocess(const Json&, const Interrupt&);
int worker(const char* input);

// Foreground work holds a lease throughout negotiation, delivery or transformation.
class Activity
{
  public:
    explicit Activity(std::atomic_uint& active) : active_{active}
    {
        ++active_;
    }
    ~Activity()
    {
        --active_;
    }
    Activity(const Activity&) = delete;
    Activity& operator=(const Activity&) = delete;

  private:
    std::atomic_uint& active_;
};

class Probe
{
  public:
    Probe(std::string node, const std::filesystem::path& directory, std::atomic_uint& activity,
          Run run = subprocess, std::function<std::string()> identify = hardware_identity,
          std::chrono::milliseconds watch = std::chrono::seconds{15});
    ~Probe();
    Probe(const Probe&) = delete;
    Probe& operator=(const Probe&) = delete;
    Json status(bool refresh_identity = false);
    Json start(bool prepare_gpu);
    bool cancel(const std::string& id, Json& reply);

  private:
    void refresh_device();
    void schedule(std::string_view reason);
    Json snapshot() const;
    void loop(const std::stop_token& stop);
    void execute(const std::stop_token& stop, const std::string& id);
    const std::string node_;
    const std::filesystem::path file_;
    std::atomic_uint& activity_;
    Run run_;
    std::function<std::string()> identify_;
    std::chrono::milliseconds watch_;
    std::mutex mutex_;
    std::condition_variable_any changed_;
    std::string identity_;
    Json job_, result_ = nullptr;
    bool pending_{};
    std::jthread thread_;
};
} // namespace bacuri::vms::acceleration
