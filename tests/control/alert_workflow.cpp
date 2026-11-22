#include "control/events/workflow.hpp"
#include <gtest/gtest.h>

namespace bacuri::vms::control::events
{
TEST(AlertWorkflow, ClaimRequiresANewAlertAtTheExpectedVersion)
{
    const AlertWork work{"new", {}, 0, false};
    EXPECT_EQ(work.evaluate(claim, "operator", 0), WorkDecision::allowed);
    EXPECT_EQ(work.evaluate(claim, "operator", 1), WorkDecision::state_conflict);
    EXPECT_EQ(work.evaluate(close, "operator", 0), WorkDecision::state_conflict);
}
TEST(AlertWorkflow, OnlyTheOwnerCanRecordAResultAndClose)
{
    const AlertWork work{"in_progress", "operator", 1, false};
    EXPECT_EQ(work.evaluate(claim, "operator", 1), WorkDecision::state_conflict);
    EXPECT_EQ(work.evaluate(record_result, "other", 1), WorkDecision::different_owner);
    EXPECT_EQ(work.evaluate(record_result, "operator", 1), WorkDecision::allowed);
    EXPECT_EQ(work.evaluate(close, "operator", 1), WorkDecision::missing_result);
    const AlertWork completed{"in_progress", "operator", 2, true};
    EXPECT_EQ(completed.evaluate(close, "other", 2), WorkDecision::different_owner);
    EXPECT_EQ(completed.evaluate(close, "operator", 2), WorkDecision::allowed);
    const AlertWork closed{"closed", "operator", 3, true};
    for (const auto& action : work_actions)
        EXPECT_EQ(closed.evaluate(action, "operator", 3), WorkDecision::state_conflict);
}
} // namespace bacuri::vms::control::events