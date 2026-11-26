#include "modules/access/oidc.hpp"
#include "storage/postgresql/query_mock.hpp"
namespace bacuri::vms::control::access
{
TEST(IdentityProvider, ReadsOnlyThePublicSettingsAndDistinguishesAnUnconfiguredSite)
{
    postgresql::test::Queries db;
    db.expect("SELECT enabled,issuer,client_id,admin_acr", {"site"});
    EXPECT_FALSE(oidc_provider(db, "site"));
    db.expect("SELECT enabled,issuer,client_id,admin_acr", {"site"},
              {{"f", "https://identity.test", "console", "mfa"}});
    EXPECT_THAT(oidc_provider(db, "site"),
                testing::Optional(testing::AllOf(testing::Field(&IdentityProvider::enabled, false),
                                                 testing::Field(&IdentityProvider::client_id, "console"),
                                                 testing::Field(&IdentityProvider::admin_acr, "mfa"))));
}
TEST(IdentityProvider, KeepsTheExistingSecretOnlyForTheSameIssuerAndClient)
{
    postgresql::test::Queries db;
    const testing::InSequence order;
    const Key key{};
    ProviderSettings settings{{false, "https://identity.test", "console", "mfa"}, {}};
    db.expect("SELECT sealed_secret", {"site", settings.provider.issuer, "console"});
    EXPECT_THAT([&] { configure_provider(db, key, "site", settings); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::invalid_request)));
    db.expect("SELECT sealed_secret", {"site", settings.provider.issuer, "console"}, {{"sealed-value"}});
    db.expect("INSERT INTO bacuri_access.identity_providers",
              {"site", "false", settings.provider.issuer, "console", "sealed-value", "mfa"});
    db.expect("DELETE FROM bacuri_access.browser_sessions", {"site"});
    db.expect("DELETE FROM bacuri_access.oidc_requests", {"site"});
    configure_provider(db, key, "site", settings);
}
TEST(ExternalIdentity, RefusesABindingAlreadyInUseAndInvalidatesSessionsWhenRemoved)
{
    postgresql::test::Queries db;
    const testing::InSequence order;
    const std::string user = "a0000000-0000-4000-8000-000000000001";
    ExternalIdentity identity{"https://identity.test", "subject"};
    db.expect("FOR UPDATE", {"site", user}, {{""}});
    db.expect("SELECT FROM bacuri_access.identity_providers", {"site", identity.issuer}, {{""}});
    db.expect("SELECT FROM bacuri_access.external_identities", {identity.issuer, identity.subject, "site", user},
              {{""}});
    EXPECT_THAT([&] { bind_identity(db, "site", user, identity); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::identity_conflict)));
    db.expect("FOR UPDATE", {"site", user}, {{""}});
    db.expect("DELETE FROM bacuri_access.external_identities", {"site", user});
    db.expect("DELETE FROM bacuri_access.browser_sessions", {"site", user});
    bind_identity(db, "site", user, {});
}
} // namespace bacuri::vms::control::access
