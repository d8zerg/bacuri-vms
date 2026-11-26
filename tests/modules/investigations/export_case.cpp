#include "application/control_api/http_json.hpp"
#include "modules/investigations/cases.hpp"
#include "modules/access/operations.hpp"
#include "application/control_api/playback_time.hpp"
#include "storage/postgresql/query_mock.hpp"

namespace bacuri::vms::control::investigations
{
namespace
{
class CaseExport : public testing::Test
{
  protected:
    postgresql::test::Queries db;
    const testing::InSequence order;
    const std::string start = utc_text(0), end = utc_text(1000000);
    Json material(const std::string& point) const
    {
        return {{"point_id", point},
                {"hold_id", "hold-" + point},
                {"interval", {{"start", start}, {"end", end}}},
                {"bookmarks", {{{"at", start}, {"name", "arrival"}}}}};
    }
    void role(const std::string& value)
    {
        db.expect("FROM bacuri_access.site_roles", {"site", "viewer"}, {{value}});
    }
    void load(const std::string& who, const Json& materials)
    {
        db.expect("owner_id=$3 OR $4='administrator'", {"site", "case", "viewer", who},
                  {{"INC-1", "owner", "notes", materials.dump(), "8"}});
    }
    void visibility(const std::string& point, bool masked, bool allowed)
    {
        db.expect("privacy_policies", {"site", point, "archive"},
                  masked ? postgresql::test::Cells{{R"([{"left":0,"top":0.2,"right":1,"bottom":0.9}])"}}
                         : postgresql::test::Cells{});
        db.expect("bacuri_access.can_use",
                  {"site", "viewer", point, "view", "0", "1000000", masked ? "" : "archive"},
                  {{allowed ? "t" : "f"}});
    }
    ExportCase lookup()
    {
        return case_for_export(db, "site", "case", "viewer");
    }
};
TEST_F(CaseExport, ReturnsTheStoredRevisionAndOrderedVisibleMaterials)
{
    role("administrator");
    load("administrator", Json::array({material("second"), material("first")}));
    visibility("second", false, true);
    visibility("first", true, true);
    const auto file = lookup();
    EXPECT_EQ(file.id, "case");
    EXPECT_EQ(file.number, "INC-1");
    EXPECT_EQ(file.assignee, "owner");
    EXPECT_EQ(file.notes, "notes");
    EXPECT_EQ(file.revision, "8");
    ASSERT_EQ(file.materials.size(), 2u);
    EXPECT_EQ(file.materials[0].point_id, "second");
    const auto& item = file.materials[1];
    EXPECT_EQ(item.point_id, "first");
    EXPECT_EQ(item.hold_id, "hold-first");
    EXPECT_EQ(item.interval.start, start);
    EXPECT_EQ(item.interval.end, end);
    ASSERT_EQ(item.bookmarks.size(), 1u);
    EXPECT_EQ(item.bookmarks.front().at, start);
    EXPECT_EQ(item.bookmarks.front().name, "arrival");
}
TEST_F(CaseExport, ConcealsCasesOutsideTheSiteRoleOrOwnership)
{
    db.expect("FROM bacuri_access.site_roles", {"site", "viewer"});
    EXPECT_THAT([&] { lookup(); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::not_found)));
    role("operator");
    db.expect("owner_id=$3 OR $4='administrator'", {"site", "case", "viewer", "operator"});
    EXPECT_THAT([&] { lookup(); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::not_found)));
}
TEST_F(CaseExport, StopsAtTheFirstInaccessibleMaterialEvenForTheAdministrator)
{
    role("administrator");
    load("administrator", Json::array({material("first"), material("denied"), material("last")}));
    visibility("first", false, true);
    visibility("denied", true, false);
    EXPECT_THAT([&] { lookup(); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::forbidden)));
}
TEST_F(CaseExport, OffersOnlyTheCurrentActorForAnEmptyCase)
{
    db.expect("SELECT username", {"viewer"}, {{"Operator"}});
    EXPECT_THAT(eligible_recipients(db, "site", "viewer", {}),
                testing::ElementsAre(testing::Pair("viewer", "Operator")));
}
TEST_F(CaseExport, RequiresViewAndProtectionBeforeLookingUpPossibleRecipients)
{
    visibility("point", false, false);
    EXPECT_THAT([&] { eligible_recipients(db, "site", "viewer", {{"point", 0, 1000000}}); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::forbidden)));
    visibility("point", false, true);
    db.expect("bacuri_access.can_use", {"site", "viewer", "point", "protect", "0", "1000000", ""}, {{"f"}});
    EXPECT_THAT([&] { eligible_recipients(db, "site", "viewer", {{"point", 0, 1000000}}); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::forbidden)));
}
TEST_F(CaseExport, ResolvesRecipientsAcrossAllAuthorizedIntervals)
{
    for (const auto* point : {"a", "b"}) {
        visibility(point, false, true);
        db.expect("bacuri_access.can_use", {"site", "viewer", point, "protect", "0", "1000000", ""}, {{"t"}});
    }
    db.expect("jsonb_to_recordset",
              {"site", R"([{"a":0,"b":1000000,"point":"a"},{"a":0,"b":1000000,"point":"b"}])"},
              {{"user", "Operator"}});
    EXPECT_THAT(eligible_recipients(db, "site", "viewer", {{"a", 0, 1000000}, {"b", 0, 1000000}}),
                testing::ElementsAre(testing::Pair("user", "Operator")));
}
} // namespace
} // namespace bacuri::vms::control::investigations
