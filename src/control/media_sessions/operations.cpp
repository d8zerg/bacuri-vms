#include "control/database.hpp"
#include "control/recovery.hpp"
#include "control/media_sessions/operations.hpp"

namespace bacuri::vms::control::media_sessions
{
std::string open_export(Database& db, const std::string& browser, const std::string& site,
                        const std::string& point, const std::string& begin, const std::string& end)
{
    return db
        .exec("INSERT INTO "
              "bacuri_media_sessions.media_sessions(browser,site_id,point_id,begin_us,end_us,expires_at) "
              "VALUES($1,$2,$3,$4,$5,now()+interval '5 minutes') RETURNING id",
              {browser, site, point, begin, end})
        .get(0);
}
void close_export(Database& db, const std::string& session)
{
    db.exec("DELETE FROM bacuri_media_sessions.media_sessions WHERE id=$1", {session});
}
bool close_archive_sessions(Database& db, const std::string& site, StorageLayout layout)
{
    const std::string sessions = layout == StorageLayout::legacy ? "bacuri_inventory.media_sessions"
                                                                 : "bacuri_media_sessions.media_sessions";
    return db
        .exec(("UPDATE " + sessions +
               " SET closed=true WHERE site_id=$1 AND begin_us IS NOT NULL AND NOT closed AND "
               "expires_at>now() RETURNING 1")
                  .c_str(),
              {site})
        .size();
}
} // namespace bacuri::vms::control::media_sessions
