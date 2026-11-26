#include "application/recovery/recovery.hpp"
#include "storage/postgresql/query_mock.hpp"
#include <sstream>
#include "storage/postgresql/configuration_snapshot.hpp"
#include <gtest/gtest.h>
namespace bacuri::vms::control
{
namespace
{
void expect_snapshot(postgresql::test::Queries& db)
{
    db.expect("BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY", {});
    EXPECT_CALL(db, exec(testing::HasSubstr("SELECT coalesce(jsonb_agg"), testing::SizeIs(0)))
        .Times(testing::AtLeast(1))
        .WillRepeatedly([](std::string_view query, auto) {
            if (query.ends_with("FROM bacuri_inventory.sites t"))
                return postgresql::test::rows({{R"([{"site_id":"assigned"}])"}});
            if (query.ends_with("FROM bacuri_access.local_users t"))
                return postgresql::test::rows({{R"([{"username":"operator"}])"}});
            return postgresql::test::rows({{"[]"}});
        });
}
} // namespace
TEST(ConfigurationBackup, SealsOwnerSectionsForTheAssignedSiteAndNode)
{
    postgresql::test::Queries db;
    const testing::InSequence order;
    expect_snapshot(db);
    db.expect("COMMIT", {});
    std::ostringstream output;
    const platform::Key key{};
    backup_configuration(db, output, "assigned", "recorder", key);
    auto encrypted = output.str();
    ASSERT_TRUE(encrypted.ends_with('\n'));
    encrypted.pop_back();
    EXPECT_THROW(platform::unseal(encrypted, key, "configuration-backup/v1/assigned/other"), std::runtime_error);
    const auto data =
        nlohmann::json::parse(platform::unseal(encrypted, key, "configuration-backup/v1/assigned/recorder"));
    EXPECT_EQ(data.at("sites").at(0).at("site_id"), "assigned");
    EXPECT_EQ(data.at("local_users").at(0).at("username"), "operator");
    EXPECT_EQ(data.at("investigations"), nlohmann::json::array());
}
TEST(ConfigurationBackup, RefusesToEmitAnotherSitesInventory)
{
    postgresql::test::Queries db;
    const testing::InSequence order;
    expect_snapshot(db);
    std::ostringstream output;
    EXPECT_THAT([&] { backup_configuration(db, output, "other", "recorder", {}); },
                testing::ThrowsMessage<std::runtime_error>(testing::HasSubstr("single-site")));
    EXPECT_TRUE(output.str().empty());
}
TEST(ConfigurationRestore, RejectsTruncatedInputBeforeOpeningTheDatabase)
{
    for (const auto* ciphertext : {"", "k1:partial"}) {
        std::istringstream backup{ciphertext};
        EXPECT_THAT([&] { restore_configuration(backup); },
                    testing::ThrowsMessage<std::runtime_error>(testing::HasSubstr("Incomplete")));
    }
}
TEST(ConfigurationSnapshot, RejectsAnotherSiteAndRequiresExactlyOneSite)
{
    nlohmann::json data{{"sites", {{{"site_id", "assigned"}}}}};
    EXPECT_NO_THROW(postgresql::ConfigurationSnapshot::require_single_site(data, "assigned"));
    EXPECT_THROW(postgresql::ConfigurationSnapshot::require_single_site(data, "other"), std::runtime_error);
    data["sites"].push_back({{"site_id", "other"}});
    EXPECT_THROW(postgresql::ConfigurationSnapshot::require_single_site(data, "assigned"), std::runtime_error);
    data["sites"].clear();
    EXPECT_THROW(postgresql::ConfigurationSnapshot::require_single_site(data, "assigned"), std::runtime_error);
}
TEST(ConfigurationRestore, WritesToAnEmptyTargetAndReplaysOnAnIdenticalTarget)
{
    const nlohmann::json empty{{"sites", nlohmann::json::array()}};
    const nlohmann::json backup{{"sites", {{{"site_id", "assigned"}}}},
                                {"credential_audit", {}},
                                {"access_audit", {}},
                                {"policy_versions", {}}};
    EXPECT_TRUE(postgresql::ConfigurationSnapshot::requires_restore(empty, backup));
    EXPECT_FALSE(postgresql::ConfigurationSnapshot::requires_restore(backup, backup));
    auto active = backup;
    active["access_audit"] = {{{"action", "login"}}};
    active["policy_versions"] = {{{"revision", 4}}};
    EXPECT_FALSE(postgresql::ConfigurationSnapshot::requires_restore(active, backup));
}
TEST(ConfigurationRestore, RefusesToOverwriteChangedInventory)
{
    const nlohmann::json backup{{"sites", {{{"site_id", "assigned"}}}},
                                {"credential_audit", {}},
                                {"access_audit", {}},
                                {"policy_versions", {}}};
    auto active = backup;
    active.at("sites").front()["site_id"] = "different";
    EXPECT_THROW(postgresql::ConfigurationSnapshot::requires_restore(active, backup), std::runtime_error);
}
} // namespace bacuri::vms::control
