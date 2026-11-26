#include "modules/events/operations.hpp"
#include "storage/postgresql/query_mock.hpp"

namespace bacuri::vms::control::events
{
TEST(ArchiveAnchor, ReturnsTheClockAndBindingOfTheAlertObservation)
{
    postgresql::test::Queries db;
    db.expect("FROM bacuri_events.alert_details", {"site", "alert"},
              {{"point", "3", "source", "4294967298000", "250"}});
    const auto anchor = archive_anchor(db, "site", "alert");
    if (!anchor)
        FAIL() << "The stored alert observation has no archive anchor";
    EXPECT_EQ(anchor->point, "point");
    EXPECT_EQ(anchor->revision, "3");
    EXPECT_EQ(anchor->basis, "source");
    EXPECT_EQ(anchor->time, 4294967298000LL);
    EXPECT_EQ(anchor->uncertainty, "250");
    db.expect("FROM bacuri_events.alert_details", {"other", "alert"});
    EXPECT_FALSE(archive_anchor(db, "other", "alert"));
}
} // namespace bacuri::vms::control::events
