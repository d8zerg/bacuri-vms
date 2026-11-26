#include "application/control_api/http_json.hpp"
#include "modules/maps/georeference_json.hpp"
#include "modules/maps/georeference.hpp"
#include "modules/maps/operations.hpp"
#include "storage/postgresql/query_mock.hpp"

namespace bacuri::vms::control
{
namespace
{
TEST(MapPlacement, LooksUpMembershipForTheRequestedSitePointAndPlan)
{
    postgresql::test::Queries db;
    db.expect("site_id=$1 AND point_id=$2 AND plan_id=$3", {"site", "point", "plan"}, {{""}});
    EXPECT_TRUE(point_on_plan(db, "site", "point", "plan"));
    db.expect("site_id=$1 AND point_id=$2 AND plan_id=$3", {"site", "point", "other"});
    EXPECT_FALSE(point_on_plan(db, "site", "point", "other"));
}
class PlanGeoreference : public testing::Test
{
  protected:
    postgresql::test::Queries db;
    const testing::InSequence order;
    const std::string plan = "a0000000-0000-4000-8000-000000000001";
    const std::string point = "a0000000-0000-4000-8000-000000000002";
    const Json first{{"x", 0.1}, {"y", 0.2}, {"lng", 30.0}, {"lat", 55.0}};
    Json definition{{"anchor", first}, {"second", {{"x", 0.8}, {"y", 0.6}, {"lng", 30.001}, {"lat", 55.001}}}};
    Json body{{"plan_id", plan}, {"definition", definition}};
    Json run(bool save)
    {
        Request req{http::verb::post, "/maps/georeference", 11};
        req.set(http::field::content_type, "application/json");
        req.body() = body.dump();
        return maps_geo::georeference(db, req, "site", save);
    }
    void preview()
    {
        db.expect("FROM bacuri_maps.floor_plans", {"site", plan}, {{"1000", "500", std::nullopt}});
        db.expect(
            "FROM bacuri_maps.point_placements", {"site", plan},
            {{point, "0.1", "0.2", "f", std::nullopt, "31", "55", std::nullopt, std::nullopt, std::nullopt}});
        const Json state = {"site", plan, "", definition, {point, "0.1", "0.2", "f", "", "31", "55", ""}};
        db.expect("SELECT encode(sha256", {state.dump()}, {{"current"}});
    }
};
TEST_F(PlanGeoreference, PreviewReportsConflictWithoutWriting)
{
    preview();
    const auto result = run(false);
    EXPECT_EQ(result.at("token"), "current");
    ASSERT_EQ(result.at("cameras").size(), 1u);
    const auto& camera = result.at("cameras").front();
    EXPECT_EQ(camera.at("point_id"), point);
    EXPECT_EQ(camera.at("existing"), Json::array({31, 55}));
    EXPECT_NEAR(camera.at("projected").at(0).get<double>(), 30.0, 1e-9);
    EXPECT_GT(camera.at("distance_m").get<double>(), 1000);
}
TEST_F(PlanGeoreference, RejectsStalePreviewOrUnresolvedConflictBeforeWriting)
{
    body["token"] = "previous";
    preview();
    EXPECT_THAT([&] { run(true); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::georeference_changed)));
    body["token"] = "current";
    preview();
    EXPECT_THAT([&] { run(true); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::invalid_georeference)));
}
TEST_F(PlanGeoreference, SavesOnlyPlacementsSelectedByTheOperator)
{
    for (const auto& choice : {"keep", "plan"}) {
        body["token"] = "current";
        body["choices"] = {{point, choice}};
        preview();
        const auto selected = postgresql::array_parameter(
            std::string_view(choice) == "plan" ? std::vector{point} : std::vector<std::string>{});
        db.expect("SET georeference=", {"site", plan, definition.dump()});
        db.expect("SET geo_primary=NULL", {"site", plan, selected});
        db.expect("SET geo_primary=true", {"site", plan, selected});
        EXPECT_EQ(run(true).at("token"), "current");
    }
}
TEST_F(PlanGeoreference, MissingPlanDoesNotReadPlacements)
{
    db.expect("FROM bacuri_maps.floor_plans", {"site", plan});
    EXPECT_THAT([&] { run(false); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::not_found)));
}
} // namespace
} // namespace bacuri::vms::control
