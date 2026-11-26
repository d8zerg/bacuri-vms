#include "control/routes.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace bacuri::vms::control
{
namespace
{
constexpr std::string_view device = "0b4f3f8e-1d2c-4a5b-9c7d-6e8f9a0b1c2d";
constexpr std::string_view item = "4c1d7e2f-8a9b-4c0d-9e1f-2a3b4c5d6e7f";

struct Recorder {
    std::string route;
    PathMatch match;
    Routes::Handler handler(const std::string& name)
    {
        return [this, name](const PathMatch& m) {
            route = name;
            match = m;
        };
    }
};

TEST(Routes, LiteralAndIdentifierSegmentsSelectTheRoute)
{
    Recorder seen;
    Routes routes;
    routes.add("cameras", seen.handler("list"))
        .add("cameras/{id}", seen.handler("camera"))
        .add("cameras/{id}/edge-archive", seen.handler("edge"));

    ASSERT_TRUE(routes.dispatch("cameras"));
    EXPECT_EQ(seen.route, "list");
    ASSERT_TRUE(routes.dispatch("cameras/" + std::string{device}));
    EXPECT_EQ(seen.route, "camera");
    ASSERT_EQ(seen.match.values.size(), 1U);
    EXPECT_EQ(seen.match.values[0], device);
    ASSERT_TRUE(routes.dispatch("cameras/" + std::string{device} + "/edge-archive"));
    EXPECT_EQ(seen.route, "edge");
}

TEST(Routes, RejectsInvalidIdentifiersAndUnknownOrIncompletePaths)
{
    Recorder seen;
    Routes routes;
    routes.add("cameras/{id}", seen.handler("camera")).add("cameras/{id}/edge-archive", seen.handler("edge"));

    EXPECT_FALSE(routes.dispatch("cameras/not-a-uuid"));
    EXPECT_FALSE(routes.dispatch("cameras/" + std::string{device} + "/unknown"));
    EXPECT_FALSE(routes.dispatch("cameras/"));
    EXPECT_FALSE(routes.dispatch("cameras/" + std::string{device} + "/"));
    // Identifiers are canonical lowercase UUIDs only.
    EXPECT_FALSE(routes.dispatch("cameras/" + std::string{"0B4F3F8E-1D2C-4A5B-9C7D-6E8F9A0B1C2D"}));
    EXPECT_TRUE(seen.route.empty());
}

TEST(Routes, NameAndRestSegmentsCaptureTheirValues)
{
    Recorder seen;
    Routes routes;
    routes.add("camera-batches/{id}/{id}/{name}", seen.handler("action"))
        .add("media-sessions/{id}", seen.handler("session"))
        .add("media-sessions/{id}/*", seen.handler("part"));

    ASSERT_TRUE(routes.dispatch("camera-batches/" + std::string{device} + "/" + std::string{item} + "/apply"));
    EXPECT_EQ(seen.route, "action");
    ASSERT_EQ(seen.match.values.size(), 3U);
    EXPECT_EQ(seen.match.values[1], item);
    EXPECT_EQ(seen.match.values[2], "apply");

    ASSERT_TRUE(routes.dispatch("media-sessions/" + std::string{device}));
    EXPECT_EQ(seen.route, "session");
    ASSERT_TRUE(routes.dispatch("media-sessions/" + std::string{device} + "/parts/7/init"));
    EXPECT_EQ(seen.route, "part");
    EXPECT_EQ(seen.match.rest, "parts/7/init");
    // A trailing slash gives an empty rest.
    ASSERT_TRUE(routes.dispatch("media-sessions/" + std::string{device} + "/"));
    EXPECT_EQ(seen.match.rest, "");
}

TEST(Routes, TheFirstMatchingPatternWins)
{
    Recorder seen;
    Routes routes;
    routes.add("alerts/{id}/archive", seen.handler("archive")).add("alerts/*", seen.handler("work"));

    ASSERT_TRUE(routes.dispatch("alerts/" + std::string{device} + "/archive"));
    EXPECT_EQ(seen.route, "archive");
    ASSERT_TRUE(routes.dispatch("alerts/" + std::string{device} + "/work"));
    EXPECT_EQ(seen.route, "work");
    EXPECT_EQ(seen.match.rest, std::string{device} + "/work");
}

TEST(Routes, RejectsMalformedPatterns)
{
    Routes routes;
    EXPECT_THROW(routes.add("cameras/*/edge-archive", {}), std::invalid_argument);
    EXPECT_THROW(routes.add("cameras//edge-archive", {}), std::invalid_argument);
    EXPECT_THROW(routes.add("cameras/{device}", {}), std::invalid_argument);
}
} // namespace
} // namespace bacuri::vms::control
