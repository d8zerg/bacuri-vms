#include "modules/inventory/device.hpp"
#include "modules/inventory/enrollment.hpp"

#include <gtest/gtest.h>

namespace bacuri::vms::control
{
namespace
{
TEST(DeviceFamily, SettingsWithoutAFamilyBelongToOnvif)
{
    EXPECT_EQ(device_family(camera_control(Json::object())).name(), "onvif");
    EXPECT_EQ(device_family(camera_control(Json{{"media_service", "http://camera/onvif/media"}})).name(), "onvif");
    EXPECT_EQ(device_family(camera_control(Json{{"family", "onvif"}})).name(), "onvif");
    EXPECT_EQ(&device_family(camera_control(Json::object())), &device_family_named("onvif"));
}

TEST(DeviceFamily, AnUnknownFamilyIsRefused)
{
    try {
        (void)device_family(camera_control(Json{{"family", "unknown"}}));
        FAIL() << "An unknown family was accepted";
    } catch (const Failure& failure) {
        EXPECT_EQ(failure.error, ErrorCode::invalid_request);
        EXPECT_EQ(failure.status(), 400);
    }
}

TEST(CameraConfiguration, KeepsServiceAvailabilityAndCredentialsSeparate)
{
    const Json stored{{"onvif", {{"serial", "s"}, {"model", "m"}, {"firmware", "f"}}},
                      {"onvif_control",
                       {{"ip", "192.0.2.8"},
                        {"host", "camera"},
                        {"profile", "main"},
                        {"PTZ", "https://camera/ptz"},
                        {"directions", {"left", "right"}}}},
                      {"credentials", {{"username", "viewer"}, {"password", ""}}},
                      {"main_url", "rtsp://camera/main"},
                      {"secondary_url", "rtsp://camera/secondary"}};
    const auto channel = camera_channel(stored);
    EXPECT_EQ(channel.credentials.username, "viewer");
    EXPECT_TRUE(channel.credentials.password.empty());
    EXPECT_EQ(channel.control.profile, "main");
    EXPECT_EQ(channel.control.ptz, "https://camera/ptz");
    EXPECT_EQ(channel.control.directions, (std::vector<std::string>{"left", "right"}));
    EXPECT_FALSE(channel.control.imaging);
    EXPECT_FALSE(channel.control.device_service);
    EXPECT_THROW(camera_credentials(Json{{"username", "viewer"}}), Json::exception);
    EXPECT_THROW(camera_control(Json{{"ip", 123}}), Json::exception);
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
