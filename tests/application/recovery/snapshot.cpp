#include "application/recovery/site.hpp"
#include <gtest/gtest.h>
namespace bacuri::vms::control
{
TEST(ConfigurationSnapshot, RejectsAnotherSiteAndRequiresExactlyOneSite)
{
    nlohmann::json data{{"sites", {{{"site_id", "assigned"}}}}};
    EXPECT_NO_THROW(require_single_site(data, "assigned"));
    EXPECT_THROW(require_single_site(data, "other"), std::runtime_error);
    data["sites"].push_back({{"site_id", "other"}});
    EXPECT_THROW(require_single_site(data, "assigned"), std::runtime_error);
    data["sites"].clear();
    EXPECT_THROW(require_single_site(data, "assigned"), std::runtime_error);
}
TEST(ConfigurationRestore, WritesToAnEmptyTargetAndReplaysOnAnIdenticalTarget)
{
    const nlohmann::json empty{{"sites", nlohmann::json::array()}};
    const nlohmann::json backup{{"sites", {{{"site_id", "assigned"}}}},
                                {"credential_audit", {}},
                                {"access_audit", {}},
                                {"policy_versions", {}}};
    EXPECT_TRUE(requires_restore(empty, backup));
    EXPECT_FALSE(requires_restore(backup, backup));
    auto active = backup;
    active["access_audit"] = {{{"action", "login"}}};
    active["policy_versions"] = {{{"revision", 4}}};
    EXPECT_FALSE(requires_restore(active, backup));
}
TEST(ConfigurationRestore, RefusesToOverwriteChangedInventory)
{
    const nlohmann::json backup{{"sites", {{{"site_id", "assigned"}}}},
                                {"credential_audit", {}},
                                {"access_audit", {}},
                                {"policy_versions", {}}};
    auto active = backup;
    active.at("sites").front()["site_id"] = "different";
    EXPECT_THROW(requires_restore(active, backup), std::runtime_error);
}
} // namespace bacuri::vms::control
