#include "media/acceleration/settings.hpp"
#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
namespace
{
using namespace bacuri::vms::acceleration;
using Json = nlohmann::json;
Json probe()
{
    return {{"stale", false}, {"result", {{"devices", Json::array()}, {"measurements", Json::array()}}}};
}
void gpu(Json& status, const char* id, unsigned sessions, std::initializer_list<const char*> workloads)
{
    auto& result = status["result"];
    result["devices"].push_back({{"id", id}, {"backend", "nvidia"}, {"memory_bytes", 1000}});
    auto& samples = result["measurements"];
    samples.push_back({{"device_id", id},
                       {"state", "measured"},
                       {"operation", "sessions"},
                       {"codec", "h264"},
                       {"verified_sessions", sessions}});
    for (const auto* codec : {"h264", "h265"}) {
        for (const auto* op : {"encode", "decode"})
            samples.push_back(
                {{"device_id", id}, {"state", "measured"}, {"operation", op}, {"codec", codec}, {"fps", 100.}});
        for (const auto* op : workloads)
            samples.push_back({{"device_id", id},
                               {"state", "measured"},
                               {"operation", op},
                               {"codec", codec},
                               {"fps", 100.},
                               {"sampled_device_memory_bytes", 200}});
    }
}
TEST(AccelerationSettings, NormalizesManualTasksAndRejectsOverrides)
{
    Settings manual{"manual", {"thumbnail", "export"}};
    EXPECT_TRUE(normalize(manual));
    EXPECT_EQ(manual.gpu_tasks, (std::vector<std::string>{"export", "thumbnail"}));
    for (Settings invalid :
         {Settings{"auto", {"export"}}, Settings{"manual", {"x"}}, Settings{"manual", {"export", "export"}}})
        EXPECT_FALSE(normalize(invalid));
}
TEST(AccelerationSettings, AccountsForSharedOutputEncoderAndPrioritizesLive)
{
    auto p = probe();
    gpu(p, "gpu", 1, {"mask", "profile", "export"});
    auto plan = evaluate({"manual", {"export", "live_mask", "live_profile"}}, p);
    EXPECT_EQ(plan["rejected_tasks"], Json({"live_profile", "export"}));
    EXPECT_EQ(plan["devices"][0]["encoder_sessions_used"], 1);
    EXPECT_EQ(plan["decisions"][0]["device_id"], "gpu");
    EXPECT_EQ(plan["decisions"][1]["reason"], "budget_exceeded");
    EXPECT_EQ(plan["decisions"][2]["reason"], "not_selected");
    EXPECT_EQ(evaluate({"auto", {}}, p)["decisions"][2]["reason"], "unsupported");
}
TEST(AccelerationSettings, FitsTasksAcrossDifferentDevices)
{
    auto p = probe();
    gpu(p, "gpu-a", 1, {"mask", "export"});
    gpu(p, "gpu-b", 1, {"mask"});
    auto plan = evaluate({"manual", {"live_mask", "export"}}, p);
    EXPECT_TRUE(plan["rejected_tasks"].empty());
    EXPECT_EQ(plan["decisions"][0]["device_id"], "gpu-b");
    EXPECT_EQ(plan["decisions"][3]["device_id"], "gpu-a");
}
TEST(AccelerationSettings, ChoosesTheLowerMeasuredCostForEquivalentAssignments)
{
    auto p = probe();
    gpu(p, "gpu-a", 1, {"mask"});
    gpu(p, "gpu-b", 1, {"mask"});
    for (auto& m : p["result"]["measurements"])
        if (m["device_id"] == "gpu-a" && m["operation"] == "mask")
            m["fps"] = 30.;
    EXPECT_EQ(evaluate({"auto", {}}, p)["decisions"][0]["device_id"], "gpu-b");
}
TEST(AccelerationSettings, CountsBothAnalysisLoadsAndUsesWorstMeasuredInput)
{
    auto p = probe();
    gpu(p, "gpu", 12, {"analysis", "thumbnail"});
    for (auto& m : p["result"]["measurements"])
        if (m["operation"] == "analysis" && m["codec"] == "h265")
            m["fps"] = 40.;
    auto plan = evaluate({"auto", {}}, p);
    EXPECT_EQ(plan["decisions"][2]["time_ppm"], 625000);
    EXPECT_EQ(plan["decisions"][4]["reason"], "budget_exceeded");
    EXPECT_EQ(plan["decisions"][6]["time_ppm"], 10000);
}
TEST(AccelerationSettings, RespectsDedicatedMemoryAndUnmeasuredWorkingSets)
{
    auto p = probe();
    gpu(p, "gpu", 12, {"mask", "analysis"});
    p["result"]["devices"][0]["memory_bytes"] = 300;
    auto plan = evaluate({"auto", {}}, p);
    EXPECT_EQ(plan["devices"][0]["memory_bytes_used"], 200);
    EXPECT_EQ(plan["decisions"][2]["reason"], "budget_exceeded");
    p["result"]["devices"][0]["memory_bytes"] = nullptr;
    for (auto& m : p["result"]["measurements"])
        m.erase("sampled_device_memory_bytes");
    plan = evaluate({"auto", {}}, p);
    EXPECT_EQ(plan["decisions"][0]["exclusive"], true);
    EXPECT_EQ(plan["decisions"][2]["reason"], "budget_exceeded");
    p["stale"] = true;
    EXPECT_TRUE(evaluate({"auto", {}}, p)["devices"].empty());
}
} // namespace
