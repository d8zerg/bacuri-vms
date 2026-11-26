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
TEST(CameraOrientations, KeepsUnobservedCamerasAndDecodesTelemetryForTheCurrentRevision)
{
    postgresql::test::Queries db;
    db.expect("LEFT JOIN bacuri_inventory.ptz_telemetry", {"site", postgresql::array_parameter({"a", "b"})},
              {{"a", "fixed", "f", std::nullopt, std::nullopt, std::nullopt, std::nullopt, "1"},
               {"b", "dome", "t", R"({"pan":-12.5,"space":"degrees","zoom":0,"moving":false})", "stamp", "f",
                "main", "9"}});
    const auto cameras = camera_orientations(db, "site", {"a", "b"});
    ASSERT_EQ(cameras.size(), 2u);
    EXPECT_FALSE(cameras.at("a").ptz);
    EXPECT_FALSE(cameras.at("a").position);
    const auto& dome = cameras.at("b");
    EXPECT_EQ(dome.revision, "9");
    EXPECT_FALSE(dome.fresh);
    EXPECT_THAT(dome.position,
                testing::Optional(testing::AllOf(testing::Field(&CameraPosition::pan, -12.5),
                                                 testing::Field(&CameraPosition::zoom, testing::Optional(0.0)),
                                                 testing::Field(&CameraPosition::moving, testing::Optional(false)),
                                                 testing::Field(&CameraPosition::hfov, std::nullopt))));
}
TEST(CameraOrientations, EmptyPointSelectionDoesNotQueryStorage)
{
    postgresql::test::Queries db;
    EXPECT_TRUE(camera_orientations(db, "site", {}).empty());
}
} // namespace bacuri::vms::control::inventory
