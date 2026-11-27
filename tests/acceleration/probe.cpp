#include "media/acceleration/probe.hpp"
#include <gtest/gtest.h>
#include <glib.h>
#include <fstream>
#include <set>

namespace
{
using namespace bacuri::vms::acceleration;
using namespace std::chrono_literals;

class NodeProbe : public testing::Test
{
  protected:
    void SetUp() override
    {
        auto* path = g_dir_make_tmp("acceleration-XXXXXX", nullptr);
        ASSERT_NE(path, nullptr);
        directory = path;
        g_free(path);
    }
    void TearDown() override
    {
        std::filesystem::remove_all(directory);
    }
    static Json sample(const Json& q, const Interrupt&)
    {
        if (q.at("stage") == "inventory")
            return {{"state", "measured"}, {"devices", Json::array({{{"id", "cpu"}, {"backend", "cpu"}}})}};
        return {{"state", "measured"}, {"fps", 50}};
    }
    static Json wait(Probe& probe, const std::function<bool(const Json&)>& accept)
    {
        const auto until = std::chrono::steady_clock::now() + 3s;
        Json value;
        do {
            value = probe.status();
            if (accept(value))
                return value;
            std::this_thread::sleep_for(2ms);
        } while (std::chrono::steady_clock::now() < until);
        throw std::runtime_error("Probe state timed out: " + value.dump());
    }
    static Json complete(Probe& probe)
    {
        return wait(probe, [](const Json& s) { return s.at("job").at("state") == "completed"; });
    }
    std::filesystem::path directory;
    std::atomic_uint activity{};
};
TEST_F(NodeProbe, CancellationAtEveryStageKeepsCompletedResult)
{
    std::atomic<int> target{-1};
    std::atomic_bool entered{};
    std::array<std::string, 5> stages{"inventory", "decode", "encode", "inference", "workloads"};
    auto run = [&](const Json& q, const Interrupt& interrupted) {
        int index = target.load();
        if (index >= 0 && q.at("stage") == stages.at(static_cast<std::size_t>(index))) {
            entered = true;
            while (!interrupted())
                std::this_thread::sleep_for(1ms);
            return Json{{"state", "interrupted_or_timeout"}};
        }
        return sample(q, interrupted);
    };
    Probe probe{"node", directory, activity, run, [] {return "a"; }, 10ms};
    auto baseline = complete(probe).at("result");
    for (int i = 0; i < 5; ++i) {
        target = i;
        entered = false;
        auto job = probe.start(i != 0).at("job");
        auto id = job.at("id").get<std::string>();
        ASSERT_EQ(job.at("reason"), i == 0 ? "administrator" : "prepare_gpu");
        ASSERT_TRUE(probe.start(true).at("job").at("prepared_for_gpu"));
        wait(probe, [&](const Json&) { return entered.load(); });
        ASSERT_EQ(probe.start(false).at("job").at("id"), id);
        Json reply;
        ASSERT_FALSE(probe.cancel("obsolete", reply));
        ASSERT_TRUE(probe.cancel(id, reply));
        EXPECT_EQ(reply.at("job").at("state"), "cancelled");
        EXPECT_EQ(reply.at("result"), baseline);
    }
}
TEST_F(NodeProbe, YieldsAndInvalidatesSavedResultOnDriverChange)
{
    std::atomic_bool replaced{};
    auto identity = [&] { return replaced.load() ? "b" : "a"; };
    Json baseline;
    {
        Probe probe{"node", directory, activity, sample, identity, 5ms};
        baseline = complete(probe).at("result");
    }
    activity = 1;
    Probe probe{"node", directory, activity, sample, identity, 5ms};
    auto waiting = wait(probe, [](const Json& s) { return s.at("job").at("state") == "waiting"; });
    EXPECT_EQ(waiting.at("result"), baseline);
    replaced = true;
    auto stale = wait(probe, [](const Json& s) { return s.at("stale") == true; });
    EXPECT_EQ(stale.at("job").at("reason"), "device_changed");
    EXPECT_EQ(stale.at("result"), baseline);
    activity = 0;
    auto current = complete(probe);
    EXPECT_EQ(current.at("stale"), false);
    EXPECT_EQ(current.at("result").at("fingerprint"), "b");
}
TEST_F(NodeProbe, ApplyingSettingsRefreshesIdentityBeforeTheNextScheduledCheck)
{
    std::atomic_uint driver{};
    Probe probe{"node", directory, activity, sample, [&] {return std::to_string(driver.load()); }, 1h};
    const auto original = complete(probe).at("result");
    activity = 1;
    ++driver;
    EXPECT_EQ(probe.status().at("stale"), false);
    const auto current = probe.status(true);
    EXPECT_EQ(current.at("stale"), true);
    EXPECT_EQ(current.at("result"), original);
    EXPECT_EQ(current.at("job").at("reason"), "device_changed");
    ++driver;
    const auto preparing = probe.start(true);
    EXPECT_NE(preparing.at("job").at("id"), current.at("job").at("id"));
    EXPECT_EQ(preparing.at("job").at("reason"), "device_changed");
}
TEST_F(NodeProbe, ActiveDeliveryInterruptsAndRetriesTheRunningSample)
{
    std::atomic_uint attempts{};
    auto run = [&](const Json& q, const Interrupt& interrupted) {
        if (q.at("stage") == "inventory" && attempts.fetch_add(1) == 0) {
            while (!interrupted())
                std::this_thread::sleep_for(1ms);
            return Json{{"state", "interrupted_or_timeout"}};
        }
        return sample(q, interrupted);
    };
    Probe probe{"node", directory, activity, run, [] { return "a"; }};
    wait(probe, [&](const Json&) { return attempts.load() == 1; });
    {
        Activity delivery{activity};
        auto status = wait(probe, [](const Json& s) { return s.at("job").at("state") == "waiting"; });
        EXPECT_EQ(status.at("job").at("steps")[0].at("completed"), 0);
    }
    EXPECT_EQ(complete(probe).at("job").at("steps")[0].at("completed"), 1);
    EXPECT_EQ(attempts.load(), 2u);
}
TEST_F(NodeProbe, FailedInventoryPreservesPreviousMeasurement)
{
    std::atomic_bool fail{};
    Probe probe{
        "node", directory, activity,
        [&](const Json& q, const Interrupt& f) { return fail.load() ? Json{{"state", "failed"}} : sample(q, f); },
        [] { return "a"; }};
    auto baseline = complete(probe).at("result");
    fail = true;
    probe.start(false);
    auto failed = wait(probe, [](const Json& s) { return s.at("job").at("state") == "failed"; });
    EXPECT_EQ(failed.at("result"), baseline);
}
} // namespace
