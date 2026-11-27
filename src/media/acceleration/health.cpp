#include "media/acceleration/health.hpp"
namespace bacuri::vms::acceleration
{
unsigned verify_device(const HealthCheck& check, const Json& device, const Interrupt& interrupted, const Run& run)
{
    using namespace std::chrono_literals;
    if (check.encoders_in_use) {
        auto sample =
            run({{"stage", "session_recovery"}, {"device", device}, {"occupied", *check.encoders_in_use}},
                interrupted);
        return sample.value("verified_sessions", 0u);
    }
    for (auto kind : check.kinds) {
        for (const auto* codec : {"h264", "h265"}) {
            Json request{{"stage", "workloads"},
                         {"operation", tasks.at(kind).operation},
                         {"device", device},
                         {"codec", codec}};
            // The driver's utilization window can still include the preceding codec's check.
            const auto until = SchedulerClock::now() + 3s;
            for (;;) {
                if (interrupted())
                    return 0;
                const auto sample = run(request, interrupted);
                const auto& state = sample.at("state");
                if (state == "measured")
                    break;
                if (state != "busy" || SchedulerClock::now() >= until)
                    return 0;
                std::this_thread::sleep_for(250ms);
            }
        }
    }
    return 1;
}
} // namespace bacuri::vms::acceleration
