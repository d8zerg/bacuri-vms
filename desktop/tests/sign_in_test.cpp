#include "site/pkce.hpp"

#include <gtest/gtest.h>

namespace bacuri::vms::client
{
namespace
{
TEST(Pkce, ComputesTheChallengeOfTheRfc7636Example)
{
    EXPECT_EQ(s256("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"), "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
}

TEST(Pkce, GeneratesFreshUnreservedValues)
{
    const auto first = new_pkce();
    const auto second = new_pkce();
    EXPECT_EQ(first.verifier.size(), 64);
    EXPECT_EQ(first.challenge.size(), 43);
    EXPECT_EQ(first.challenge, s256(first.verifier));
    EXPECT_NE(first.verifier, second.verifier);
    EXPECT_NE(first.state, second.state);
    for (const auto character : first.verifier + first.state)
        EXPECT_TRUE(character.isLetterOrNumber() || character == QLatin1Char{'-'} || character == QLatin1Char{'_'})
            << static_cast<int>(character.unicode());
}

TEST(Callback, ReadsTheCodeOrTheRefusalForTheSameState)
{
    const auto code = read_callback("GET /callback?code=abc&state=s1 HTTP/1.1\r\n", "/callback", "s1");
    ASSERT_TRUE(code);
    EXPECT_EQ(code->code, "abc");
    const auto refused =
        read_callback("GET /callback?error=access_denied&state=s1 HTTP/1.1\r\n", "/callback", "s1");
    ASSERT_TRUE(refused);
    EXPECT_EQ(refused->error, "access_denied");
}

TEST(Callback, IgnoresOtherRequests)
{
    EXPECT_FALSE(read_callback("GET /callback?code=abc&state=other HTTP/1.1", "/callback", "s1"));
    EXPECT_FALSE(read_callback("GET /callback?code=abc HTTP/1.1", "/callback", "s1"));
    EXPECT_FALSE(read_callback("GET /favicon.ico HTTP/1.1", "/callback", "s1"));
    EXPECT_FALSE(read_callback("POST /callback?code=abc&state=s1 HTTP/1.1", "/callback", "s1"));
    EXPECT_FALSE(read_callback("GET /callback?code=a&error=b&state=s1 HTTP/1.1", "/callback", "s1"));
    EXPECT_FALSE(read_callback("GET /callback?state=s1 HTTP/1.1", "/callback", "s1"));
    EXPECT_FALSE(read_callback("GET /callback?code=abc&state= HTTP/1.1", "/callback", ""));
}
} // namespace
} // namespace bacuri::vms::client
