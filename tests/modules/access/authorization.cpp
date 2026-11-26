#include "modules/access/operations.hpp"
#include "modules/access/session.hpp"
#include "storage/postgresql/query_mock.hpp"

namespace bacuri::vms::control
{
namespace
{
class BrowserAuthentication : public testing::Test
{
  protected:
    const Key key{};
    const std::string origin{"https://site.local"}, cookie{std::string(64, 'a')};
    const std::string site{"a0000000-0000-4000-8000-000000000001"};
    const Sessions sessions{key, origin};
    postgresql::test::Queries db;
    Request request{http::verb::post, "/api/v1/sites/" + site + "/maps", 11};
    void SetUp() override
    {
        request.set(http::field::cookie, "other=x; __Host-bacuri_session=" + cookie);
        request.set(http::field::origin, origin);
        request.set("X-CSRF-Token", fingerprint(key, "csrf", cookie));
    }
    auto authenticate()
    {
        return sessions.authenticate(db, request, true);
    }
    void active(const std::string& scope)
    {
        db.expect("UPDATE bacuri_access.browser_sessions", {fingerprint(key, "session", cookie)},
                  {{"operator", scope}});
    }
};
TEST_F(BrowserAuthentication, UpdatesActivityAndReturnsIdentityForSiteMutation)
{
    active(site);
    const auto identity = authenticate();
    EXPECT_EQ(identity.user, "operator");
    EXPECT_EQ(identity.cookie, cookie);
}
TEST_F(BrowserAuthentication, PassiveLookupUsesTheReadOnlySessionQuery)
{
    db.expect("SELECT user_id,oidc_site", {fingerprint(key, "session", cookie)}, {{"operator", std::nullopt}});
    request.erase(http::field::origin);
    request.erase("X-CSRF-Token");
    EXPECT_EQ(sessions.authenticate(db, request, false, false).user, "operator");
}
TEST_F(BrowserAuthentication, RefusesMalformedCookieBeforeReadingTheSession)
{
    request.set(http::field::cookie, "__Host-bacuri_session=" + std::string(64, 'z'));
    EXPECT_THAT([&] { authenticate(); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::unauthenticated)));
}
TEST_F(BrowserAuthentication, RefusesAnExpiredOrUnknownSession)
{
    db.expect("UPDATE bacuri_access.browser_sessions", {fingerprint(key, "session", cookie)});
    EXPECT_THAT([&] { authenticate(); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::unauthenticated)));
}
TEST_F(BrowserAuthentication, RefusesACorporateSessionScopedToAnotherSite)
{
    active("a0000000-0000-4000-8000-000000000002");
    EXPECT_THAT([&] { authenticate(); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::forbidden)));
}
TEST_F(BrowserAuthentication, RefusesDuplicateOrMismatchedCsrfTokens)
{
    request.insert("X-CSRF-Token", fingerprint(key, "csrf", cookie));
    active(site);
    EXPECT_THAT([&] { authenticate(); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::forbidden)));
    request.set("X-CSRF-Token", "wrong");
    active(site);
    EXPECT_THAT([&] { authenticate(); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::forbidden)));
}
class ExportPermission : public testing::Test
{
  protected:
    postgresql::test::Queries db;
    void view(bool permitted)
    {
        db.expect("bacuri_access.can_use", {"site", "user", "point", "view", "10", "20", ""},
                  {{permitted ? "t" : "f"}});
    }
    std::string grant(bool derivative)
    {
        return access::export_permission(db, "site", "point", "user", "10", "20", derivative);
    }
};
TEST_F(ExportPermission, RefusesBothFormatsWithoutViewPermission)
{
    for (const bool derivative : {false, true}) {
        view(false);
        EXPECT_THAT([&] { grant(derivative); },
                    testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::forbidden)));
    }
}
TEST_F(ExportPermission, MasksTheEntireImageForAnEmptyPolicyAndKeepsExplicitRegions)
{
    const testing::InSequence order;
    db.expect("privacy_policies", {"site", "point"});
    EXPECT_TRUE(access::export_regions(db, "site", "point").empty());
    db.expect("privacy_policies", {"site", "point"}, {{"[]"}});
    EXPECT_THAT(access::export_regions(db, "site", "point"),
                testing::ElementsAre(domain::ImageRegion{0, 0, 1, 1}));
    db.expect("privacy_policies", {"site", "point"}, {{R"([{"left":0.2,"top":0,"right":1,"bottom":0.8}])"}});
    EXPECT_THAT(access::export_regions(db, "site", "point"),
                testing::ElementsAre(domain::ImageRegion{0.2, 0, 1, 0.8}));
}
TEST_F(ExportPermission, PrivacyPolicyWithEmptyRegionsBlocksOriginalExport)
{
    const testing::InSequence order;
    view(true);
    db.expect("privacy_policies", {"site", "point"}, {{"[]"}});
    EXPECT_THAT([&] { grant(false); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::forbidden)));
}
TEST_F(ExportPermission, ReturnsTheGrantForTheRequestedIntervalAndFormat)
{
    for (const bool derivative : {false, true}) {
        const testing::InSequence order;
        view(true);
        if (!derivative)
            db.expect("privacy_policies", {"site", "point"});
        db.expect("effective_grants", {"site", "point", "user", "10", "20", derivative ? "true" : "false"},
                  {{"grant"}});
        EXPECT_EQ(grant(derivative), "grant");
    }
}
TEST_F(ExportPermission, RequiresAnExportGrantInAdditionToViewPermission)
{
    const testing::InSequence order;
    view(true);
    db.expect("effective_grants", {"site", "point", "user", "10", "20", "true"});
    EXPECT_THAT([&] { grant(true); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::forbidden)));
}
} // namespace
} // namespace bacuri::vms::control
