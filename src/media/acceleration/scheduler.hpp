#pragma once
#include "media/acceleration/budget.hpp"
#include "media/acceleration/settings.hpp"
#include <chrono>
#include <functional>
#include <map>
#include <set>

namespace bacuri::vms::acceleration
{
using SchedulerClock = std::chrono::steady_clock;
using Instant = SchedulerClock::time_point;
enum class Report { poll, released, session_limit, device_lost, driver_reset, encoder_error, finished };
struct TaskUpdate {
    std::string id;
    std::size_t kind;
    bool wait;
    Report report;
    std::string released;
    std::string running{}, running_grant{};
    bool cpu_until_checkpoint{};
};
struct Assignment {
    std::string device, grant, reason;
};
struct Transition {
    std::string task, kind, from, to, reason, device;
};
struct HealthCheck {
    std::string device;
    std::set<std::size_t> kinds;
    std::uint64_t generation{};
    std::optional<unsigned> encoders_in_use;
};
// One owner serializes updates. Outstanding grants reserve resources until release or expiry.
class Scheduler
{
  public:
    static constexpr auto lease = std::chrono::seconds{4};
    static constexpr auto cooldown = std::chrono::seconds{30};
    using Notify = std::function<void(const Transition&)>;
    explicit Scheduler(Notify notify, Instant start);
    void configure(Settings policy, std::vector<Device> devices, bool stale);
    void tick(Instant now);
    Assignment update(const TaskUpdate& request, Instant now);
    std::optional<HealthCheck> check_due(Instant now);
    void checked(const HealthCheck& check, unsigned verified, Instant now);
    nlohmann::json status() const;

  private:
    struct Job {
        std::size_t kind;
        bool wait;
        std::uint64_t order;
        Instant expires, promote, grant_until;
        Assignment assignment;
        std::string held, running;
        bool cpu_until_checkpoint{};
        Cost charge;
    };
    struct Fault {
        std::string reason;
        std::optional<unsigned> sessions;
        std::set<std::size_t> kinds;
        std::uint64_t generation{};
        Instant retry;
        bool checking{};
    };
    void expire(Instant now);
    void plan(Instant now, const Job* polling = nullptr);
    void move(const std::string& id, Job& job, const std::string& target, std::string reason);
    void fault(const Job& job, Report report, Instant now);
    Notify notify_;
    const Instant startup_;
    std::uint64_t sequence_{};
    Settings policy_;
    std::array<std::string, tasks.size()> preferred_;
    std::vector<Device> devices_;
    bool stale_{};
    std::map<std::string, Job> jobs_;
    std::map<std::string, Fault> faults_;
};
} // namespace bacuri::vms::acceleration
