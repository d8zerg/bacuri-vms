#include "media/acceleration/health.hpp"
#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
namespace
{
using namespace bacuri::vms::acceleration;
using namespace std::chrono_literals;
class NodeScheduler : public testing::Test
{
  protected:
    std::vector<Transition> events;
    Instant now{Instant{} + 10s};
    Device gpu{"gpu", 1, 1000, {}, {}};
    Scheduler scheduler{[this](const Transition& event) { events.push_back(event); }, Instant{}};
    void SetUp() override
    {
        for (std::size_t i = 0; i < tasks.size(); ++i)
            gpu.costs[i] = Cost{200000, tasks[i].encoder ? 1u : 0u, 100, false};
        scheduler.configure({"auto", {}}, {gpu}, false);
    }
    Assignment update(std::string id, std::size_t kind, Report report = Report::poll, std::string grant = {})
    {
        return scheduler.update({std::move(id), kind, true, report, std::move(grant)}, now);
    }
};
TEST_F(NodeScheduler, GrantsLiveOnlyAfterTheExportReleasesItsEncoder)
{
    auto exp = update("export", 3);
    ASSERT_EQ(exp.device, "gpu");
    EXPECT_EQ(update("live", 0).reason, "awaiting_release");
    auto revoke = update("export", 3);
    EXPECT_EQ(revoke.device, "queue");
    EXPECT_EQ(revoke.grant, exp.grant);
    const auto paths = scheduler.status().at("assignments");
    const auto& retiring = paths.back();
    EXPECT_EQ(retiring.at("task"), "export");
    EXPECT_EQ(retiring.at("device_id"), "queue");
    EXPECT_EQ(retiring.at("releasing_device_id"), "gpu");
    EXPECT_EQ(scheduler.status()["devices"][0]["encoder_sessions_used"], 1);
    update("export", 3, Report::released, exp.grant);
    auto live = update("live", 0);
    EXPECT_EQ(live.device, "gpu");
    EXPECT_EQ(update("live", 0, Report::released, exp.grant).grant, live.grant);
    update("live", 0, Report::finished, live.grant);
    for (int i = 1; i < 30; ++i) {
        now += 1s;
        EXPECT_EQ(update("export", 3).device, "queue");
    }
    now += 1s;
    EXPECT_EQ(update("export", 3).device, "gpu");
    EXPECT_EQ(scheduler.status()["tasks"]["gpu"], 1);
}
TEST_F(NodeScheduler, ReclaimsExpiredGrantsAndKeepsEqualPriorityInArrivalOrder)
{
    EXPECT_EQ(update("z", 0).device, "gpu");
    EXPECT_EQ(update("a", 0).device, "cpu");
    now += 3s;
    EXPECT_EQ(update("a", 0).device, "cpu");
    now += 1s;
    EXPECT_EQ(update("a", 0).device, "gpu");
    EXPECT_EQ(scheduler.status()["tasks"]["cpu"], 0);
}
TEST_F(NodeScheduler, ExternalSessionLimitPreemptsExportAndKeepsTheNewLiveStream)
{
    gpu.sessions = 2;
    scheduler.configure({"auto", {}}, {gpu}, false);
    auto exp = update("export", 3);
    auto live = update("live", 0);
    ASSERT_EQ(live.device, "gpu");
    auto retry = update("live", 0, Report::session_limit, live.grant);
    EXPECT_EQ(retry.device, "cpu");
    EXPECT_EQ(update("export", 3).device, "queue");
    EXPECT_EQ(update("live", 0).reason, "awaiting_release");
    update("export", 3, Report::released, exp.grant);
    auto renewed = update("live", 0);
    EXPECT_EQ(renewed.device, "gpu");
    EXPECT_NE(renewed.grant, live.grant);
    EXPECT_EQ(update("live", 0, Report::driver_reset, live.grant).grant, renewed.grant);
    EXPECT_EQ(scheduler.status()["warnings"][0]["reason"], "session_limit");
    for (int i = 0; i < 30; ++i) {
        now += 1s;
        update("live", 0);
        update("export", 3);
    }
    auto check = scheduler.check_due(now);
    if (!check)
        FAIL() << "Expected a due health check";
    EXPECT_EQ(check->encoders_in_use, 1u);
    scheduler.checked(*check, 1, now);
    EXPECT_EQ(update("export", 3).device, "gpu");
}
TEST_F(NodeScheduler, FaultRequiresFreshSuccessfulCheckAndLeavesOtherDeviceAvailable)
{
    auto second = gpu;
    second.id = "other";
    second.costs[0].reset();
    scheduler.configure({"auto", {}}, {gpu, second}, false);
    auto live = update("live", 0);
    auto exp = update("export", 3);
    ASSERT_EQ(exp.device, "other");
    EXPECT_EQ(update("live", 0, Report::driver_reset, live.grant).device, "cpu");
    for (int i = 0; i < 30; ++i) {
        now += 1s;
        update("live", 0);
        EXPECT_EQ(update("export", 3).grant, exp.grant);
    }
    auto check = scheduler.check_due(now);
    if (!check)
        FAIL() << "Expected a due health check";
    scheduler.checked(*check, 0, now);
    EXPECT_FALSE(scheduler.check_due(now));
    for (int i = 0; i < 30; ++i) {
        now += 1s;
        EXPECT_EQ(update("live", 0).device, "cpu");
    }
    check = scheduler.check_due(now);
    if (!check)
        FAIL() << "Expected a due health check";
    scheduler.checked(*check, 1, now);
    EXPECT_EQ(update("live", 0).device, "gpu");
    EXPECT_TRUE(scheduler.status()["warnings"].empty());
}
TEST_F(NodeScheduler, RecoveryRetriesBusyTelemetryBetweenCodecSamples)
{
    const HealthCheck check{"gpu", {0}, 1, {}};
    std::vector<std::string> codecs;
    const auto count = verify_device(
        check, Json::object(), [] { return false; },
        [&](const Json& sample, const Interrupt&) {
            codecs.push_back(sample.at("codec").get<std::string>());
            return Json{{"state", codecs.size() == 2 ? "busy" : "measured"}};
        });
    EXPECT_EQ(count, 1u);
    EXPECT_EQ(codecs, (std::vector<std::string>{"h264", "h265", "h265"}));
}
TEST_F(NodeScheduler, ChecksEveryReportedWorkloadBeforeClearingADeviceFault)
{
    gpu.sessions = 2;
    scheduler.configure({"auto", {}}, {gpu}, false);
    const auto a = update("video", 0);
    const auto b = update("analysis", 2);
    update("video", 0, Report::encoder_error, a.grant);
    update("analysis", 2, Report::device_lost, b.grant);
    now += Scheduler::cooldown;
    scheduler.tick(now);
    const auto check = scheduler.check_due(now);
    if (!check)
        FAIL() << "Expected a due health check";
    EXPECT_EQ(check->kinds, (std::set<std::size_t>{0, 2}));
}
TEST_F(NodeScheduler, UsesFreeDeviceBeforePreemptingAndThenChoosesTheCheaperGpu)
{
    auto other = gpu;
    other.id = "second";
    other.costs[0] = Cost{400000, 1, 100, false};
    scheduler.configure({"auto", {}}, {gpu, other}, false);
    EXPECT_EQ(update("export", 3).device, "gpu");
    EXPECT_EQ(update("live", 0).device, "second");
    EXPECT_EQ(update("export", 3).device, "gpu");
}
TEST_F(NodeScheduler, ManualSelectionPreservesTheOnlyDeviceCapableOfExport)
{
    auto other = gpu;
    other.id = "second";
    other.costs[3].reset();
    scheduler.configure({"manual", {"live_mask", "export"}}, {gpu, other}, false);
    EXPECT_EQ(update("live", 0).device, "second");
    EXPECT_EQ(update("export", 3).device, "gpu");
}
TEST_F(NodeScheduler, ManualSelectionCpuModeStaleProbeAndBudgetsApplyToEveryInstance)
{
    gpu.sessions = 9;
    gpu.memory = 210;
    scheduler.configure({"manual", {"live_mask"}}, {gpu}, false);
    EXPECT_EQ(update("export", 3).reason, "not_selected");
    EXPECT_EQ(update("a", 0).device, "gpu");
    EXPECT_EQ(update("b", 0).device, "gpu");
    EXPECT_EQ(update("c", 0).reason, "budget_exceeded");
    const auto assignments = scheduler.status().at("assignments");
    EXPECT_EQ(
        assignments,
        Json::array({{{"task", "live_mask"}, {"device_id", "cpu"}, {"reason", "budget_exceeded"}, {"count", 1}},
                     {{"task", "live_mask"}, {"device_id", "gpu"}, {"reason", "within_budget"}, {"count", 2}},
                     {{"task", "export"}, {"device_id", "queue"}, {"reason", "not_selected"}, {"count", 1}}}));
    scheduler.configure({"auto", {}}, {}, true);
    EXPECT_EQ(update("a", 0).reason, "device_changed");
    scheduler.configure({}, {gpu}, false);
    EXPECT_EQ(update("c", 0).reason, "cpu_only");
}
TEST_F(NodeScheduler, ReleasedEncoderCanBeReclaimedAtTheLastGpuDeadline)
{
    auto exp = update("e", 3);
    EXPECT_EQ(update("l", 0).device, "cpu");
    now += 3s;
    EXPECT_EQ(update("e", 3).grant, exp.grant);
    EXPECT_EQ(update("l", 0).device, "cpu");
    now += 1s;
    EXPECT_EQ(update("l", 0).device, "gpu");
    EXPECT_TRUE(update("e", 3).grant.empty());
}
} // namespace
