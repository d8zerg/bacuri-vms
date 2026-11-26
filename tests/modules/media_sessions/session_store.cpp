#include "modules/media_sessions/export_scope_json.hpp"
#include "modules/recordings/archive_recovery.hpp"
#include "storage/postgresql/query_mock.hpp"

namespace bacuri::vms::control::media_sessions
{
TEST(ExportSession, StoresTheAuthorizedScopeSeparatelyFromTheSourceBlockExtents)
{
    postgresql::test::Queries db;
    const testing::InSequence order;
    const ExportScope scope{"grant", "12", "25", {{0, 0, 1, 1}}, true, false};
    const auto encoded =
        nlohmann::json{
            {"begin", "12"},           {"end", "25"},
            {"derivative", true},      {"sound", false},
            {"export_grant", "grant"}, {"regions", R"([{"bottom":1.0,"left":0.0,"right":1.0,"top":0.0}])"}}
            .dump();
    db.expect("interval '5 minutes'", {"browser", "site", "camera", "10", "30", encoded}, {{"session"}});
    const auto session = open_export(db, "browser", "site", "camera", "10", "30", scope);
    EXPECT_EQ(session, "session");
    const auto restored = nlohmann::json::parse(encoded).get<ExportScope>();
    EXPECT_EQ(restored.grant, scope.grant);
    EXPECT_EQ(restored.regions, scope.regions);
    EXPECT_TRUE(restored.derivative);
    EXPECT_FALSE(restored.sound);
    db.expect("DELETE FROM bacuri_media_sessions.media_sessions", {"session"});
    close_export(db, session);
}
TEST(ArchiveSessions, ClosesOnlyActiveArchiveSessionsAndReportsWhetherAnyChanged)
{
    postgresql::test::Queries db;
    for (const auto layout : {StorageLayout::legacy, StorageLayout::module_schemas}) {
        const auto* table = layout == StorageLayout::legacy ? "bacuri_inventory.media_sessions"
                                                            : "bacuri_media_sessions.media_sessions";
        const auto query =
            std::string("UPDATE ") + table +
            " SET closed=true WHERE site_id=$1 AND begin_us IS NOT NULL AND NOT closed AND expires_at>now()";
        db.expect(query, {"site"}, {{"1"}});
        EXPECT_TRUE(close_archive_sessions(db, "site", layout));
        db.expect(query, {"site"});
        EXPECT_FALSE(close_archive_sessions(db, "site", layout));
    }
}
} // namespace bacuri::vms::control::media_sessions
