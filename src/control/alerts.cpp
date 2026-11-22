#include "control/access/operations.hpp"
#include "control/alerts.hpp"
#include "control/playback_time.hpp"
namespace bacuri::vms::control
{
namespace
{
constexpr std::string_view fields =
    "d.alert_id,d.point_id,d.observed_at_utc_us,d.type,d.time_basis,d.uncertainty_us,d.position,d.work_state";
Json alert(const Rows& rows, int row)
{
    const auto time = std::stoll(rows.get(row, 2));
    Json value{{"id", rows.get(row)},
               {"point_id", rows.get(row, 1)},
               {"observed_at", ProtoTime::ToString(ProtoTime::MicrosecondsToTimestamp(time))},
               {"type", rows.get(row, 3)},
               {"work_state", rows.get(row, 7)},
               {"time_basis", rows.get(row, 4)},
               {"archive_interval", alert_window(time).json()}};
    if (auto uncertainty = rows.get(row, 5); !uncertainty.empty()) {
        value["uncertainty_us"] = uncertainty;
    }
    return value;
}
FeedBatch resync(std::string_view reason)
{
    return {{Json{{"type", "resync_required"}, {"reason", reason}}.dump()}, 1000};
}
} // namespace
AlertFeed::Scope AlertFeed::scope(Database& db) const
{
    db.exec("BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY");
    db.exec("SET LOCAL statement_timeout='1s'");
    auto who = sessions_.authenticate(db, request_, false, false);
    const bool admin = access::site_role(db, site_, who.user, access::RoleLock::read) == "administrator";
    const auto rows = db.exec(R"sql(
        SELECT coalesce(v.revision,0),coalesce(h.position,0),
               coalesce((SELECT min(position) FROM bacuri_events.alert_outbox WHERE site_id=r.site_id),coalesce(h.position,0)+1),coalesce(v.work_revision,0)
        FROM (SELECT $1::uuid AS site_id,$2::uuid AS user_id) r LEFT JOIN bacuri_events.access_versions v USING(user_id,site_id)
        LEFT JOIN bacuri_events.feed_heads h USING(site_id) WHERE r.site_id=$1 AND r.user_id=$2
    )sql",
                              {site_, who.user});
    return {who.user,
            Json::array({"alerts", site_, who.user, fingerprint(key_, "session", who.cookie)}).dump(),
            std::stoll(rows.get(0)),
            std::stoll(rows.get(0, 1)),
            std::stoll(rows.get(0, 2)),
            std::stoll(rows.get(0, 3)),
            admin};
}
std::string AlertFeed::cursor(const Scope& rights, std::int64_t position, bool pending) const
{
    return seal(key_, rights.context,
                Json::array({rights.revision, position, pending, rights.work_revision}).dump())
        .substr(3);
}
void AlertFeed::authorize()
{
    Database db;
    scope(db);
    db.exec("COMMIT");
}
Json AlertFeed::snapshot()
{
    Database db;
    const auto rights = scope(db);
    const auto query =
        "SELECT " + std::string{fields} +
        " FROM bacuri_events.alert_details d "
        "WHERE site_id=$1 AND ($3::boolean OR point_id=ANY($2::uuid[])) ORDER BY position DESC LIMIT 100";
    auto rows =
        db.exec(query.c_str(),
                {site_, rights.admin ? "{}" : array_parameter(access::granted_point_ids(db, site_, rights.user)),
                 rights.admin ? "true" : "false"});
    auto items = Json::array();
    for (int row = rows.size(); row > 0; --row) {
        items.push_back(alert(rows, row - 1));
    }
    db.exec("COMMIT");
    return {{"items", std::move(items)}, {"cursor", cursor(rights, rights.head)}};
}
FeedBatch AlertFeed::poll(std::string_view after)
{
    Database db;
    const auto rights = scope(db);
    if (!subscribed_) {
        try {
            auto state = Json::parse(unseal("k1:" + std::string{after}, key_, rights.context));
            revision_ = state.at(0).get<std::int64_t>();
            position_ = state.at(1).get<std::int64_t>();
            pending_ = state.at(2).get<bool>();
            work_revision_ = state.size() > 3 ? state.at(3).get<std::int64_t>() : 0;
        } catch (const std::exception&) {
            return resync("cursor_invalid");
        }
        if (position_ < 0 || position_ > rights.head) {
            return resync("cursor_invalid");
        }
        resume_head_ = rights.head;
        subscribed_ = true;
    }
    if (revision_ != rights.revision) {
        return resync("scope_changed");
    }
    if (position_ < rights.floor - 1 || (pending_ && position_ < rights.floor)) {
        return resync("cursor_expired");
    }
    if (work_revision_ != rights.work_revision)
        return resync("work_changed");
    FeedBatch batch;
    if (position_ == rights.head && !pending_) {
        return batch;
    }
    const auto query = "SELECT " + std::string{fields} + R"sql(,
        (SELECT a.alert_id FROM bacuri_events.alerts a
         WHERE a.site_id=d.site_id AND ($4::boolean OR a.point_id=ANY($2::uuid[])) AND a.position<d.position ORDER BY a.position DESC OFFSET 99 LIMIT 1)
        FROM bacuri_events.alert_outbox o JOIN bacuri_events.alert_details d USING(site_id,position)
        WHERE o.site_id=$1 AND ($4::boolean OR d.point_id=ANY($2::uuid[])) AND o.position >= $3 ORDER BY o.position LIMIT 65
    )sql";
    auto rows =
        db.exec(query.c_str(),
                {site_, rights.admin ? "{}" : array_parameter(access::granted_point_ids(db, site_, rights.user)),
                 std::to_string(position_ + (pending_ ? 0 : 1)), rights.admin ? "true" : "false"});
    if (position_ >= resume_head_ && rows.size() > 64) {
        return resync("client_lag");
    }
    for (int i = 0; i < std::min(rows.size(), 8); ++i) {
        const auto seq = std::stoll(rows.get(i, 6));
        if (auto removed = rows.get(i, 8); !removed.empty() && !(pending_ && position_ == seq)) {
            batch.frames.push_back(
                Json{{"type", "remove"}, {"cursor", cursor(rights, seq, true)}, {"alert_id", removed}}.dump());
        }
        batch.frames.push_back(
            Json{{"type", "upsert"}, {"cursor", cursor(rights, seq)}, {"alert", alert(rows, i)}}.dump());
        position_ = seq;
        pending_ = false;
    }
    if (rows.size() <= 8) {
        position_ = rights.head;
    }
    db.exec("COMMIT");
    return batch;
}
} // namespace bacuri::vms::control
