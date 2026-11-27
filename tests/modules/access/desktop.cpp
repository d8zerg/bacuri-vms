#include "modules/access/desktop.hpp"

#include <gtest/gtest.h>

namespace bacuri::vms::control::desktop
{
TEST(DesktopSignIn, AcceptsTheVerifierOfTheRfc7636Example)
{
    EXPECT_TRUE(verifier_matches("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk",
                                 "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
}

TEST(DesktopSignIn, RefusesAnotherVerifierAndMalformedValues)
{
    constexpr std::string_view challenge = "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM";
    EXPECT_FALSE(verifier_matches("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXK", challenge));
    EXPECT_FALSE(verifier_matches("short", challenge));
    EXPECT_FALSE(verifier_matches(std::string(129, 'a'), challenge));
    EXPECT_FALSE(verifier_matches("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjX+", challenge));
    EXPECT_FALSE(verifier_matches("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk",
                                  "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-c"));
    EXPECT_FALSE(valid_challenge("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-c="));
}

TEST(DesktopSignIn, AcceptsOnlyLoopbackRedirectsWithAnExplicitPort)
{
    EXPECT_TRUE(loopback_redirect("http://127.0.0.1:49152/callback"));
    EXPECT_TRUE(loopback_redirect("http://[::1]:1024/"));
    EXPECT_TRUE(loopback_redirect("http://127.0.0.1:65535/sign-in/done"));
    EXPECT_FALSE(loopback_redirect("https://127.0.0.1:49152/callback"));
    EXPECT_FALSE(loopback_redirect("http://localhost:49152/callback"));
    EXPECT_FALSE(loopback_redirect("http://127.0.0.1/callback"));
    EXPECT_FALSE(loopback_redirect("http://127.0.0.1:80/callback"));
    EXPECT_FALSE(loopback_redirect("http://127.0.0.1:65536/callback"));
    EXPECT_FALSE(loopback_redirect("http://127.0.0.1:049152/callback"));
    EXPECT_FALSE(loopback_redirect("http://127.0.0.1:49152"));
    EXPECT_FALSE(loopback_redirect("http://127.0.0.1:49152/callback?next=https://site.test"));
    EXPECT_FALSE(loopback_redirect("http://127.0.0.1:49152.evil.test/callback"));
    EXPECT_FALSE(loopback_redirect("http://127.0.0.1:49152/" + std::string(120, 'a')));
}

TEST(DesktopSignIn, ReadsOnlyDesktopTokensFromTheAuthorizationHeader)
{
    const std::string hex(64, 'a');
    EXPECT_EQ(session_token("Bearer d1." + hex), hex);
    EXPECT_FALSE(session_token("Bearer " + hex)) << "an alert delegation token";
    EXPECT_FALSE(session_token("Bearer d1." + std::string(64, 'A')));
    EXPECT_FALSE(session_token("Bearer d1." + hex + "0"));
    EXPECT_FALSE(session_token("Basic d1." + hex));
}
} // namespace bacuri::vms::control::desktop
