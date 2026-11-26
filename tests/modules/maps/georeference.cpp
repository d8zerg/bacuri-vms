#include "modules/maps/georeference.hpp"

#include <gtest/gtest.h>

namespace bacuri::vms::control::maps_geo
{
TEST(PlanTransform, MapsBothSurveyedAnchorsToTheirGeographicCoordinates)
{
    const Json first{{"x", 0.1}, {"y", 0.2}, {"lng", 30.0}, {"lat", 55.0}};
    const Json second{{"x", 0.8}, {"y", 0.6}, {"lng", 30.001}, {"lat", 55.001}};
    const PlanTransform transform{{{"anchor", first}, {"second", second}}, 1000, 500};
    for (const auto& anchor : {first, second}) {
        const auto position = transform.project({anchor.at("x"), anchor.at("y")});
        EXPECT_NEAR(position.lng, anchor.at("lng").get<double>(), 1e-9);
        EXPECT_NEAR(position.lat, anchor.at("lat").get<double>(), 1e-9);
    }
}

TEST(PlanTransform, RejectsCoincidentAnchors)
{
    const Json anchor{{"x", 0.5}, {"y", 0.5}, {"lng", 30.0}, {"lat", 55.0}};
    const Json definition{{"anchor", anchor}, {"second", anchor}};
    try {
        const PlanTransform transform{definition, 1000, 500};
        FAIL() << "Accepted coincident anchors";
    } catch (const Failure& failure) {
        EXPECT_EQ(failure.error, ErrorCode::invalid_georeference);
    }
}
} // namespace bacuri::vms::control::maps_geo
