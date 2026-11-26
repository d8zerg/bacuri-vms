#include "control/inventory/device.hpp"

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
        EXPECT_EQ(failure.status, 400);
        EXPECT_EQ(failure.code, "invalid_request");
    }
}
} // namespace
} // namespace bacuri::vms::control
