#include "application/control_api/api.hpp"
#include "modules/media_sessions/layouts.hpp"

#include <gmock/gmock.h>

namespace bacuri::vms::control::media_sessions
{
namespace
{
const std::string first = "a0000000-0000-4000-8000-000000000001";
const std::string second = "a0000000-0000-4000-8000-000000000002";
using Visibility = testing::StrictMock<testing::MockFunction<bool(const std::string&)>>;

TEST(MonitorLayout, PreservesTheOrderOfVisiblePoints)
{
    Visibility visible;
    const testing::InSequence order;
    EXPECT_CALL(visible, Call(second)).WillOnce(testing::Return(true));
    EXPECT_CALL(visible, Call(first)).WillOnce(testing::Return(true));
    EXPECT_EQ(layout_points({second, first}, visible.AsStdFunction()), (std::vector<std::string>{second, first}));
}

TEST(MonitorLayout, RejectsDuplicateSlotsBeforeCheckingTheirVisibilityAgain)
{
    Visibility visible;
    EXPECT_CALL(visible, Call(first)).WillOnce(testing::Return(true));
    try {
        layout_points({first, first}, visible.AsStdFunction());
        FAIL() << "Accepted a duplicate slot";
    } catch (const Failure& failure) {
        EXPECT_EQ(failure.error, ErrorCode::invalid_request);
    }
}

TEST(MonitorLayout, StopsAtTheFirstInaccessiblePoint)
{
    Visibility visible;
    EXPECT_CALL(visible, Call(second)).WillOnce(testing::Return(false));
    try {
        layout_points({second, first}, visible.AsStdFunction());
        FAIL() << "Accepted an inaccessible point";
    } catch (const Failure& failure) {
        EXPECT_EQ(failure.error, ErrorCode::not_found);
    }
}
} // namespace
} // namespace bacuri::vms::control::media_sessions
