#include "recording/policy_json.hpp"
#include "recording/plan.hpp"
#include "schedule.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <random>

namespace bacuri::vms::recording
{
namespace
{
using namespace std::chrono;
using namespace std::chrono_literals;
using Json = nlohmann::json;
using test::weekly;

TEST(RecordingPolicy, BothOccurrencesOfCivilHourUseHalfOpenUtcWindows)
{
    const auto policy = recording::parse_capture_rule(weekly());
    auto sunday = sys_days{2025y / October / 26};
    auto selected = [&](auto t) {
        return policy.selects(duration_cast<microseconds>(t.time_since_epoch()).count(), 1);
    };
    EXPECT_FALSE(selected(sunday + 9min));
    EXPECT_TRUE(selected(sunday + 10min));
    EXPECT_FALSE(selected(sunday + 30min));
    EXPECT_TRUE(selected(sunday + 70min));
    EXPECT_FALSE(selected(sunday + 90min));
    auto spring = sys_days{2025y / March / 30};
    for (auto minutes = 0min; minutes < 180min; minutes += 1min)
        EXPECT_FALSE(selected(spring + minutes));
}
TEST(RecordingPolicy, OvernightAndOverlappingWindowsIncludeOnlyCurrentBinding)
{
    auto rule = weekly();
    rule["schedule"] =
        Json::array({{{"day", 7}, {"start", 1380}, {"end", 60}}, {{"day", 1}, {"start", 30}, {"end", 90}}});
    auto policy = recording::parse_capture_rule(rule);
    policy.windows = {{5, 100, 200}, {5, 150, 250}, {6, 500, 600}};
    EXPECT_FALSE(policy.selects(99, 5));
    EXPECT_TRUE(policy.selects(100, 5));
    EXPECT_TRUE(policy.selects(249, 5));
    EXPECT_FALSE(policy.selects(250, 5));
    EXPECT_FALSE(policy.selects(201, 6));
    auto utc = sys_days{2025y / January / 5} + 23h + 45min;
    EXPECT_TRUE(policy.selects(duration_cast<microseconds>(utc.time_since_epoch()).count(), 1));
    rule["zone"] = "Nowhere/Imaginary";
    EXPECT_THROW(recording::parse_capture_rule(rule), std::invalid_argument);
}
class RecordingPlanTest : public testing::Test
{
  protected:
    std::filesystem::path root{std::filesystem::temp_directory_path() /
                               ("bacuri-plan-" + std::to_string(std::random_device{}()))};

    void SetUp() override
    {
        ASSERT_TRUE(std::filesystem::create_directory(root));
        storage::Sqlite journal{root / "journal.db"};
        journal.exec("CREATE TABLE recording_plan(singleton INTEGER PRIMARY KEY, payload TEXT NOT NULL)");
    }
    void TearDown() override
    {
        std::filesystem::remove_all(root);
    }
};

TEST_F(RecordingPlanTest, RestoresOnlyTheLastCommittedRules)
{
    const Json entry{{"id", "scheduled"},
                     {"revision", 3},
                     {"since", 100},
                     {"rule", weekly()},
                     {"windows", Json::array({{4, 120, 150}})}};
    {
        RecordingPlan plan{root};
        EXPECT_FALSE(plan.rule("point"));
        plan.update(decode_plan(Json{{"point", entry}}));
        const auto active = plan.rule("point");
        ASSERT_TRUE(active);
        EXPECT_TRUE(active->selects(120, 4));
        EXPECT_FALSE(active->selects(120, 5));
        EXPECT_TRUE(plan.rule("another")->continuous);
        plan.update(decode_plan(Json{{"point", entry}}));
        EXPECT_EQ(active, plan.rule("point"));
        EXPECT_THROW(plan.update(decode_plan(Json::array())), std::invalid_argument);
        EXPECT_THROW(plan.update(decode_plan(Json::parse(R"({"point":{}})"))), Json::exception);
        EXPECT_EQ(active, plan.rule("point"));
    }
    {
        RecordingPlan restored{root};
        auto rule = restored.rule("point");
        ASSERT_TRUE(rule);
        EXPECT_EQ(rule->id, "scheduled");
        EXPECT_EQ(rule->revision, 3);
        EXPECT_EQ(rule->since, 100);
        EXPECT_TRUE(rule->selects(149, 4));
        EXPECT_FALSE(rule->selects(150, 4));
        restored.update({});
        EXPECT_TRUE(restored.rule("point")->continuous);
        EXPECT_FALSE(rule->continuous);
    }
    RecordingPlan empty{root};
    ASSERT_TRUE(empty.rule("point"));
    EXPECT_TRUE(empty.rule("point")->continuous);
}

TEST_F(RecordingPlanTest, FailedPersistenceLeavesActiveRulesIntact)
{
    RecordingPlan plan{root};
    plan.update({});
    storage::Sqlite journal{root / "journal.db"};
    journal.exec(
        "CREATE TRIGGER reject_update BEFORE UPDATE ON recording_plan BEGIN SELECT RAISE(ABORT,'rejected'); END");
    Json next{{"point",
               {{"id", "next"}, {"revision", 2}, {"since", 0}, {"rule", weekly()}, {"windows", Json::array()}}}};
    EXPECT_THROW(plan.update(decode_plan(next)), std::runtime_error);
    EXPECT_TRUE(plan.rule("point")->continuous);
    RecordingPlan reopened{root};
    EXPECT_TRUE(reopened.rule("point")->continuous);
}
} // namespace
} // namespace bacuri::vms::recording
