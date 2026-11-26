#include "modules/maps/orientation.hpp"
#include "modules/maps/georeference.hpp"

#include <gtest/gtest.h>

namespace bacuri::vms::control::maps_geo
{
TEST(PlanTransform, MapsBothSurveyedAnchorsToTheirGeographicCoordinates)
{
    const SurveyAnchor first{0.1, 0.2, 30.0, 55.0};
    const SurveyAnchor second{0.8, 0.6, 30.001, 55.001};
    const PlanTransform transform{{first, second, {}}, 1000, 500};
    for (const auto& anchor : {first, second}) {
        const auto position = transform.project({anchor.x, anchor.y});
        EXPECT_NEAR(position.lng, anchor.lng, 1e-9);
        EXPECT_NEAR(position.lat, anchor.lat, 1e-9);
    }
}

TEST(PlanTransform, RejectsInvalidSurveyCoordinatesAndScale)
{
    const SurveyAnchor anchor{0.5, 0.5, 30.0, 55.0};
    for (const auto& definition :
         {PlanDefinition{{2, 0, 30, 55}, SurveyScale{40, 0}, {}},
          PlanDefinition{anchor, SurveyAnchor{0, 0, 30, 90}, {}}, PlanDefinition{anchor, SurveyScale{0, 0}, {}},
          PlanDefinition{anchor, SurveyScale{40, 361}, {}}}) {
        EXPECT_THROW((PlanTransform{definition, 1000, 500}), Failure);
    }
}
TEST(PlanTransform, RejectsCoincidentAnchors)
{
    const SurveyAnchor anchor{0.5, 0.5, 30.0, 55.0};
    const PlanDefinition definition{anchor, anchor, {}};
    try {
        const PlanTransform transform{definition, 1000, 500};
        FAIL() << "Accepted coincident anchors";
    } catch (const Failure& failure) {
        EXPECT_EQ(failure.error, ErrorCode::invalid_georeference);
    }
}
} // namespace bacuri::vms::control::maps_geo

namespace bacuri::vms::control::maps_view
{
TEST(CameraSector, RequiresTheBoundDeviceProfileAndCoordinateSpace)
{
    const Calibration cfg{"device", 350., 80., PanCalibration{"profile", "degrees", 0., 1., 20.}};
    inventory::CameraOrientation camera{"device",
                                        "profile",
                                        "1",
                                        true,
                                        true,
                                        "time",
                                        inventory::CameraPosition{20., "degrees", {}, {}, {}, {}, {}}};
    EXPECT_FALSE(sector({}, camera).bearing);
    camera.device_id = "replacement";
    EXPECT_FALSE(sector(cfg, camera).bearing);
    camera.device_id = "device";
    camera.profile = "other";
    EXPECT_FALSE(sector(cfg, camera).bearing);
    camera.profile = "profile";
    camera.position->space = "normalized";
    EXPECT_FALSE(sector(cfg, camera).bearing);
    camera.position.reset();
    EXPECT_FALSE(sector(cfg, camera).bearing);
}
TEST(CameraSector, WrapsTheBearingAndUsesReportedFieldOfViewBeforeZoomEstimation)
{
    const Calibration cfg{"device", 350., 80., PanCalibration{"profile", "degrees", 0., 1., 20.}};
    inventory::CameraOrientation camera{"device",
                                        "profile",
                                        "1",
                                        true,
                                        true,
                                        "time",
                                        inventory::CameraPosition{20., "degrees", {}, 0.5, 37., {}, {}}};
    const auto live = sector(cfg, camera);
    EXPECT_EQ(live.state, ViewState::live);
    EXPECT_EQ(live.bearing, 10);
    EXPECT_EQ(live.hfov, 37);
    EXPECT_FALSE(live.estimated);
    camera.position->hfov.reset();
    camera.fresh = false;
    const auto stale = sector(cfg, camera);
    EXPECT_EQ(stale.state, ViewState::stale);
    EXPECT_EQ(stale.observed_at, "time");
    EXPECT_EQ(stale.hfov, 50);
    EXPECT_TRUE(stale.estimated);
    camera.position->zoom.reset();
    EXPECT_EQ(sector(cfg, camera).hfov, 80);
}
TEST(CameraSector, FixedCameraNeedsNoTelemetry)
{
    const Calibration cfg{"fixed", 87., 69., {}};
    inventory::CameraOrientation camera{"fixed", "", "1", false, false, "", {}};
    const auto view = sector(cfg, camera);
    EXPECT_EQ(view.state, ViewState::fixed);
    EXPECT_EQ(view.bearing, 87);
    EXPECT_EQ(view.hfov, 69);
}
} // namespace bacuri::vms::control::maps_view
