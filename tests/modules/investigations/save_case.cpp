#include "application/control_api/http_json.hpp"
#include "modules/investigations/case_json.hpp"
#include "application/control_api/playback_time.hpp"
#include "storage/postgresql/query_mock.hpp"

namespace bacuri::vms::control::investigations
{
namespace
{
TEST(CaseMaterialInput, AcceptsClientFieldsAndRejectsAServerAssignedHold)
{
    const std::string point = "10000000-0000-4000-8000-000000000001";
    Json body{{"point_id", point},
              {"interval", {{"start", utc_text(0)}, {"end", utc_text(1000000)}}},
              {"bookmarks", Json::array()}};
    EXPECT_EQ(body.get<CaseMaterialInput>().point_id, point);
    body["hold_id"] = point;
    EXPECT_THAT([&] { return body.get<CaseMaterialInput>(); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::invalid_request)));
    EXPECT_EQ(body.get<CaseMaterial>().hold_id, point);
}
class CaseReplacement : public testing::Test
{
  protected:
    postgresql::test::Queries db;
    const testing::InSequence order;
    const CaseInterval interval{utc_text(0), utc_text(1000000)};
    ExportCase old{"case", "INC", "operator", "notes", "4", {{"previous", "old-hold", interval, {}}}};
    CaseUpdate change{"case",     "INC",  "recipient",
                      "changed",  "4",    {{"next", interval, {{utc_text(0), "arrival"}}}},
                      "evidence", 2000000};
    std::string save()
    {
        return save_case(db, "site", "operator", "operator", change, &old);
    }
    void owner(bool allowed)
    {
        db.expect("FOR SHARE", {"site", "recipient"}, {{"operator"}});
        db.expect("bacuri_access.can_use", {"site", "operator", "previous", "protect", "0", "1000000", ""},
                  {{allowed ? "t" : "f"}});
    }
    void visibility(const std::string& user, bool allowed)
    {
        db.expect("privacy_policies", {"site", "next", "archive"});
        db.expect("bacuri_access.can_use", {"site", user, "next", "view", "0", "1000000", "archive"},
                  {{allowed ? "t" : "f"}});
    }
};
TEST_F(CaseReplacement, RejectsStaleRevisionBeforeChangingHolds)
{
    change.revision = "3";
    EXPECT_THAT([&] { save(); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::revision_conflict)));
}
TEST_F(CaseReplacement, RequiresProtectionOnRemovedMaterialAndVisibilityForTheNewRecipient)
{
    owner(false);
    EXPECT_THAT([&] { save(); }, testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::forbidden)));
    owner(true);
    visibility("recipient", false);
    EXPECT_THAT([&] { save(); }, testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::forbidden)));
}
TEST_F(CaseReplacement, PreservesOldHoldIfReplacementHasNoRetainedMedia)
{
    owner(true);
    visibility("recipient", true);
    visibility("operator", true);
    db.expect("bacuri_access.can_use", {"site", "operator", "next", "protect", "0", "1000000", ""}, {{"t"}});
    db.expect("FROM bacuri_archive.retained_blocks", {"site", "next", "0", "1000000"});
    EXPECT_THAT([&] { save(); },
                testing::Throws<Failure>(testing::Field(&Failure::error, ErrorCode::media_unavailable)));
}
TEST_F(CaseReplacement, AcquiresReplacementBeforeReleasingOldHoldAndPersistsTheSameHoldInTheCase)
{
    using testing::_;
    using testing::ElementsAre;
    using testing::HasSubstr;
    owner(true);
    visibility("recipient", true);
    visibility("operator", true);
    db.expect("bacuri_access.can_use", {"site", "operator", "next", "protect", "0", "1000000", ""}, {{"t"}});
    db.expect("FROM bacuri_archive.retained_blocks", {"site", "next", "0", "1000000"}, {{"1"}});
    std::string hold;
    EXPECT_CALL(db, exec(HasSubstr("INSERT INTO bacuri_archive.archive_holds"),
                         ElementsAre("site", _, "next", "0", "1000000", "recipient", "evidence", "2000000")))
        .WillOnce([&](const char*, std::initializer_list<std::string> args) {
            hold = args.begin()[1];
            EXPECT_TRUE(canonical_id(hold));
            return postgresql::test::rows({{hold}});
        });
    EXPECT_CALL(db, exec(HasSubstr("INSERT INTO bacuri_archive.held_blocks"),
                         ElementsAre("site", testing::Eq(testing::ByRef(hold)), "next", "0", "1000000")))
        .WillOnce(testing::Return(postgresql::test::rows({})));
    db.expect("bacuri_events.alert_audit", {"site", "next", "operator", "archive_hold", "evidence", "{}"});
    db.expect("DELETE FROM bacuri_archive.archive_holds", {"site", "old-hold"});
    db.expect("number=$2 AND id<>$3", {"site", "INC", "case"});
    EXPECT_CALL(db,
                exec(HasSubstr("WHERE c.revision=$8"), ElementsAre("site", "case", "INC", "recipient", "changed",
                                                                   _, "2000000", "4", "operator", "operator")))
        .WillOnce([&](const char*, std::initializer_list<std::string> args) {
            const auto& input = change.materials.front();
            const CaseMaterial expected{input.point_id, hold, input.interval, input.bookmarks};
            EXPECT_EQ(Json::parse(args.begin()[5]), Json::array({expected}));
            return postgresql::test::rows({{"5"}});
        });
    db.expect("bacuri_events.alert_audit", {"site", "next", "operator", "case_change", "INC - evidence", "{}"});
    EXPECT_EQ(save(), "5");
}
} // namespace
} // namespace bacuri::vms::control::investigations
