#include "modules/inventory/device.hpp"
#include "modules/inventory/enrollment.hpp"

#include <gtest/gtest.h>

namespace bacuri::vms::control
{
namespace
{
TEST(DeviceFamily, SettingsWithoutAFamilyBelongToOnvif)
{
    EXPECT_EQ(device_family(Json::object()).name(), "onvif");
    EXPECT_EQ(device_family(Json{{"media_service", "http://camera/onvif/media"}}).name(), "onvif");
    EXPECT_EQ(device_family(Json{{"family", "onvif"}}).name(), "onvif");
    EXPECT_EQ(&device_family(Json::object()), &device_family_named("onvif"));
}

TEST(DeviceFamily, AnUnknownFamilyIsRefused)
{
    try {
        (void)device_family(Json{{"family", "unknown"}});
        FAIL() << "An unknown family was accepted";
    } catch (const Failure& failure) {
        EXPECT_EQ(failure.error, ErrorCode::invalid_request);
        EXPECT_EQ(failure.status(), 400);
    }
}

TEST(CameraEnrollment, RefusesToBindProfilesFromDifferentPhysicalChannels)
{
    const Key key{};
    const std::string main = "rtsp://camera/main", secondary = "rtsp://camera/secondary";
    const Json profiles{{fingerprint(key, "onvif-stream", main), {{"channel", "north"}}},
                        {fingerprint(key, "onvif-stream", secondary), {{"channel", "south"}}}};
    try {
        inventory::channel_configuration(key, {{"profiles", profiles}}, main, secondary);
        FAIL() << "Accepted profiles from different physical channels";
    } catch (const Failure& failure) {
        EXPECT_EQ(failure.error, ErrorCode::camera_incompatible_profiles);
    }
}
} // namespace
} // namespace bacuri::vms::control
