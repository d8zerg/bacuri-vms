#include "application/export/periods.hpp"
#include <gtest/gtest.h>
namespace bacuri::vms::control
{
TEST(ExportManifest, RecordsOnlyNonemptyGapsWithTheirReason)
{
    Json periods = Json::array();
    add_gap(periods, 10, 10, "missing");
    add_gap(periods, 11, 10, "missing");
    EXPECT_TRUE(periods.empty());
    add_gap(periods, 10, 15, "missing");
    EXPECT_EQ(periods,
              Json::array({{{"state", "gap"}, {"begin_utc_us", 10}, {"end_utc_us", 15}, {"reason", "missing"}}}));
}
} // namespace bacuri::vms::control
