#pragma once
#include "control.grpc.pb.h"
#include "media/acceleration/probe.hpp"
#include "media/acceleration/scheduler.hpp"
#include <deque>

namespace bacuri::vms::media
{
class Scheduling
{
  public:
    Scheduling(std::string node, acceleration::Probe& probe, std::atomic_uint& activity);
    grpc::Status update(const v1::TaskUpdate& request, v1::TaskAssignment& reply);
    acceleration::Json status();

  private:
    void event(const acceleration::Transition& change);
    void publish();
    void sample_devices(const std::stop_token& stop);
    void monitor(const std::stop_token& stop);
    void verify(acceleration::HealthCheck check, const acceleration::Json& result);
    const std::string node_;
    acceleration::Probe& probe_;
    std::atomic_uint& activity_;
    std::mutex mutex_;
    std::deque<acceleration::Json> journal_;
    std::map<std::string, acceleration::Json> factories_;
    acceleration::Scheduler scheduler_;
    std::optional<acceleration::Activity> gpu_activity_;
    bool checking_{};
    std::jthread health_, monitor_, telemetry_;
};
} // namespace bacuri::vms::media
