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
} // namespace bacuri::vms::control
