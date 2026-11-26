#include "platform/camera_networks.hpp"
#include "platform/secrets.hpp"
#include <gtest/gtest.h>
#include <sodium.h>

namespace bacuri::vms::platform
{
namespace
{

TEST(CameraNetworks, RequiresAnAllowedAddressAndStreamUri)
{
    const CameraNetworks networks{"10.0.0.0/24,2001:db8::/64"};
    EXPECT_FALSE(networks.permits("rtsp://10.0.0.5:8554/main"));
    const CameraNetworks legacy{"10.0.0.0/24", {.addresses = "10.0.0.5,10.0.1.2"}};
    EXPECT_TRUE(legacy.permits("rtsp://10.0.0.5/main"));
    const std::string profile = "rtsp://10.0.0.5/main?transmode=unicast&profile=video_main";
    EXPECT_EQ(legacy.stream_url(profile), profile);
    EXPECT_FALSE(legacy.permits(profile + "&password=x"));
    EXPECT_FALSE(legacy.permits("rtsp://10.0.0.6/main"));
    EXPECT_FALSE(legacy.permits("rtsp://10.0.1.2/main"));
    EXPECT_TRUE(networks.permits("rtsps://[2001:db8::1]/video"));
    EXPECT_FALSE(networks.permits("rtsp://10.0.1.1/main"));
    EXPECT_FALSE(networks.permits("http://10.0.0.5/main"));
    EXPECT_FALSE(networks.permits("rtsp://username@10.0.0.5/main"));
    EXPECT_FALSE(networks.permits("rtsp://10.0.0.5/main?token=x"));
    EXPECT_THROW(CameraNetworks{"10.0.0.0/64"}, std::invalid_argument);
}

TEST(CameraNetworks, SendsDirectedDiscoveryOnlyToIndividualIpv4Hosts)
{
    const CameraNetworks networks{
        "10.0.0.0/24,10.0.2.9/32,10.0.2.9/32,2001:db8::1/128,0.0.0.0/32,239.0.0.1/32,255.255.255.255/32"};
    EXPECT_EQ(networks.discovery_hosts(), std::vector<std::string>{"10.0.2.9"});
}

TEST(SealedConfiguration, AuthenticatesOwnerRevisionAndKey)
{
    ASSERT_GE(sodium_init(), 0);
    Key key{};
    randombytes_buf(key.data(), key.size());
    const auto encrypted = seal(key, "site/device/1", "sample");
    EXPECT_NE(encrypted, seal(key, "site/device/1", "sample"));
    EXPECT_EQ(unseal(encrypted, key, "site/device/1"), "sample");
    EXPECT_THROW((void)unseal(encrypted, key, "site/device/2"), std::runtime_error);
    Key other{};
    EXPECT_THROW((void)unseal(encrypted, other, "site/device/1"), std::runtime_error);
    auto damaged = encrypted;
    damaged.back() = damaged.back() == '1' ? '0' : '1';
    EXPECT_THROW((void)unseal(damaged, key, "site/device/1"), std::runtime_error);
}
} // namespace
} // namespace bacuri::vms::platform
