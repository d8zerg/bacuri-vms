#include "application/control_api/http_json.hpp"
#include "modules/events/video_json.hpp"
#include "storage/postgresql/query_mock.hpp"
namespace bacuri::vms::control::events
{
TEST(VideoContext, PreservesTheEventOrderAndTheExactVendorMetadataInTheResponse)
{
    postgresql::test::Queries db;
    db.expect("ORDER BY o.observed_at_utc_us", {"site", "point", "10", "100", "motion"},
              {{"15", "id1", "1", "topic", "onvif", "camera1", "alert"},
               {"20", "id2", "0", "motion", "external", "camera2", std::nullopt}});
    auto events = video_context(db, "site", "point", 10, 100, "motion");
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].at, 15);
    EXPECT_EQ(events[1].at, 20);
    db.expect("ORDER BY k.n",
              {"site", "point", postgresql::array_parameter({"camera1", "camera2"}),
               postgresql::array_parameter({"id1", "id2"})},
              {{R"({"ticks":9007199254740993})"}, {"{}"}});
    const auto page = video_events_json(db, "site", "point", events);
    EXPECT_EQ(page[0].at("metadata").at("ticks"), 9007199254740993LL);
    EXPECT_EQ(page[1].at("metadata"), Json::object());
    EXPECT_EQ(page[0].at("alert_id"), "alert");
    EXPECT_EQ(page[1].at("alert_id"), "");
    EXPECT_EQ(video_events_json(db, "site", "point", {}), Json::array());
}
} // namespace bacuri::vms::control::events
