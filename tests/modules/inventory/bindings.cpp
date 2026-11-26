#include "modules/inventory/operations.hpp"
#include "storage/postgresql/query_mock.hpp"

namespace bacuri::vms::control::inventory
{
TEST(RecordingBindings, MapsEachPointToItsLatestNumericRevision)
{
    postgresql::test::Queries db;
    db.expect("max(binding_revision)", {"site"}, {{"south", "4294967297"}, {"north", "2"}});
    const auto bindings = recording_bindings(db, "site");
    ASSERT_EQ(bindings.size(), 2u);
    EXPECT_EQ(bindings.at("south"), 4294967297LL);
    EXPECT_EQ(bindings.at("north"), 2);
    db.expect("max(binding_revision)", {"other"});
    EXPECT_TRUE(recording_bindings(db, "other").empty());
}
} // namespace bacuri::vms::control::inventory
