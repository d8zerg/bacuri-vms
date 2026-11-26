#include "control/database.hpp"
#include "archive_sql.hpp"
#include "control/recordings/operations.hpp"
#include "control/api.hpp"

namespace bacuri::vms::control::recordings
{
std::vector<CoverageInterval> edge_gaps(Database& db, const std::string& site, const std::string& point,
                                        const std::string& binding, std::int64_t since, std::int64_t until)
{
    require_available(db, site);
    auto rows = db.exec(R"sql(
        WITH limits AS (SELECT greatest($4::bigint,bacuri_archive.point_cutoff($1,$2)) AS a,$5::bigint AS b),
        source_gaps AS (SELECT int8range(greatest(begin_utc_us,a),least(end_utc_us,b),'[)') AS span
          FROM bacuri_archive.gaps CROSS JOIN limits WHERE site_id=$1 AND point_id=$2 AND binding_revision=$3
          AND reason='source_offline' AND greatest(begin_utc_us,a)<least(end_utc_us,b)
          ORDER BY begin_utc_us DESC LIMIT 128),
        remainder AS (SELECT span::int8multirange - coalesce((SELECT range_agg(int8range(begin_utc_us,end_utc_us,'[)'))
          FROM bacuri_archive.retained_blocks WHERE site_id=$1 AND point_id=$2 AND binding_revision=$3
          AND unavailable_reason IS NULL AND int8range(begin_utc_us,end_utc_us,'[)') && span),'{}'::int8multirange) AS missing FROM source_gaps)
        SELECT lower(part),upper(part) FROM (SELECT unnest(range_agg(missing)) AS part FROM remainder) pieces
        ORDER BY lower(part) LIMIT 128)sql",
                        {site, point, binding, std::to_string(since), std::to_string(until)});
    std::vector<CoverageInterval> result;
    result.reserve(static_cast<std::size_t>(rows.size()));
    for (int i = 0; i < rows.size(); ++i)
        result.push_back({std::stoll(rows.get(i)), std::stoll(rows.get(i, 1)), {}, {}, {}, {}});
    return result;
}

void require_available(Database& db, const std::string& site)
{
    if (db.exec("SELECT FROM bacuri_archive.archive_recovery WHERE site_id=$1", {site}).size())
        throw Failure{503, "media_unavailable"};
}
std::map<std::string, LastRecorded> last_recorded(Database& db, const std::string& site,
                                                  const std::vector<std::string>& points)
{
    std::map<std::string, LastRecorded> result;
    if (points.empty())
        return result;
    // Reception time is compared with the server clock, so a camera clock offset does not affect freshness.
    auto rows = db.exec(R"sql(
        SELECT p.point_id,b.end_utc_us,b.received_at_utc_us+(b.end_utc_us-b.begin_utc_us)
        FROM unnest($2::uuid[]) AS p(point_id)
        CROSS JOIN LATERAL (
            SELECT end_utc_us,begin_utc_us,received_at_utc_us FROM bacuri_archive.blocks
            WHERE site_id=$1 AND point_id=p.point_id AND unavailable_reason IS NULL
            ORDER BY end_utc_us DESC LIMIT 1) b
    )sql",
                        {site, array_parameter(points)});
    for (int i = 0; i < rows.size(); ++i)
        result.insert_or_assign(rows.get(i), LastRecorded{.end_utc_us = std::stoll(rows.get(i, 1)),
                                                          .received_until_us = std::stoll(rows.get(i, 2))});
    return result;
}

std::optional<PreviousRecording> previous_binding_recording(Database& db, const std::string& site,
                                                            const std::string& point, const std::string& revision)
{
    require_available(db, site);
    auto r = db.exec("SELECT binding_revision,begin_utc_us,end_utc_us FROM bacuri_archive.retained_blocks "
                     "WHERE site_id=$1 AND point_id=$2 AND binding_revision<$3 AND unavailable_reason IS NULL "
                     "ORDER BY binding_revision DESC,begin_utc_us DESC LIMIT 1",
                     {site, point, revision});
    if (!r.size())
        return {};
    return PreviousRecording{r.get(0), std::stoll(r.get(0, 1)), std::stoll(r.get(0, 2))};
}
std::int64_t export_lifetime_ms(Database& db, const std::string& site, const std::string& point,
                                const std::string& hashes)
{
    auto r = db.exec(R"sql(
        SELECT least(1000,floor((min(greatest(bacuri_archive.hold_until(b.site_id,b.block_id)::numeric, b.end_utc_us::numeric+
            least((SELECT days FROM bacuri_archive.archive_retention WHERE site_id=$1),(SELECT days FROM bacuri_archive.point_retention WHERE site_id=$1 AND point_id=$2))*86400000000::bigint))-bacuri_archive.retention_now_us())/1000))::bigint
        FROM jsonb_each_text($3::jsonb) source
        LEFT JOIN bacuri_archive.retained_blocks b ON b.site_id=$1 AND b.point_id=$2 AND b.block_id=source.key::uuid
            AND b.manifest#>>'{media,sha256}'=source.value AND b.unavailable_reason IS NULL
        HAVING count(b.block_id)=count(*) AND NOT EXISTS(SELECT FROM bacuri_archive.archive_recovery WHERE site_id=$1)
    )sql",
                     {site, point, hashes});
    return r.size() ? std::stoll(r.get(0)) : 0;
}
std::vector<std::string> manifests_for_export(Database& db, const std::string& site, const std::string& point,
                                              const std::string& begin, const std::string& end,
                                              const std::string& revision)
{
    auto rows = db.exec(
        "SELECT manifest FROM bacuri_archive.retained_blocks WHERE site_id=$1 AND point_id=$2 AND "
        "int8range(begin_utc_us,end_utc_us,'[)') && int8range($3,$4,'[)') AND unavailable_reason IS NULL AND "
        "($5='' OR binding_revision=nullif($5,'')::bigint) ORDER BY begin_utc_us,end_utc_us LIMIT 257",
        {site, point, begin, end, revision});
    return rows.column();
}
std::vector<CoverageInterval> coverage(Database& db, const std::string& site, const std::string& point,
                                       const std::string& begin, const std::string& end,
                                       const std::string& pending, const std::string& revision)
{
    auto rows = db.exec(archive::sql::search, {site, point, begin, end, pending, revision});
    std::vector<CoverageInterval> intervals;
    intervals.reserve(static_cast<std::size_t>(rows.size()));
    for (int i = 0; i < rows.size(); ++i)
        intervals.push_back({std::stoll(rows.get(i)), std::stoll(rows.get(i, 1)), rows.get(i, 2), rows.get(i, 3),
                             rows.get(i, 4), rows.get(i, 5)});
    return intervals;
}

std::int64_t retention_cutoff(Database& db, const std::string& site, const std::string& point)
{
    return std::stoll(db.exec("SELECT bacuri_archive.point_cutoff($1,$2)", {site, point}).get(0));
}

bool bind_playback_parts(Database& db, const std::string& session, const std::string& site,
                         const std::string& point, const std::string& begin, const std::string& end,
                         const std::string& revision)
{
    auto rows = db.exec(
        "INSERT INTO "
        "bacuri_archive.playback_parts(session_id,site_id,block_id,recording_epoch_id,owner_generation) "
        "SELECT $1,site_id,block_id,recording_epoch_id,owner_generation FROM bacuri_archive.retained_blocks WHERE "
        "site_id=$2 AND point_id=$3 "
        "AND (nullif($6,'')::bigint IS NULL OR binding_revision=nullif($6,'')::bigint) AND "
        "begin_utc_us>=$4 AND end_utc_us<=$5 AND unavailable_reason IS NULL RETURNING media_id",
        {session, site, point, begin, end, revision});
    return rows.size() > 0;
}

std::vector<ManifestPart> playback_manifest(Database& db, const std::string& session)
{
    auto rows = db.exec("SELECT init_id,media_id,begin_utc_us,end_utc_us,manifest#>>'{media,tracks,0,codec}' FROM "
                        "bacuri_archive.playback_parts JOIN "
                        "bacuri_archive.retained_blocks "
                        "USING(site_id,block_id,recording_epoch_id,owner_generation) WHERE session_id=$1 AND "
                        "unavailable_reason IS NULL "
                        "ORDER BY begin_utc_us,end_utc_us,media_id",
                        {session});
    std::vector<ManifestPart> parts;
    parts.reserve(static_cast<std::size_t>(rows.size()));
    for (int i = 0; i < rows.size(); ++i)
        parts.push_back(
            {rows.get(i), rows.get(i, 1), rows.get(i, 4), std::stoll(rows.get(i, 2)), std::stoll(rows.get(i, 3))});
    return parts;
}

std::optional<DeliveryPart> playback_part(Database& db, const std::string& session, const std::string& part)
{
    auto rows = db.exec(
        "SELECT block_id,manifest#>>'{media,sha256}',init_id=$2,begin_utc_us,end_utc_us FROM "
        "bacuri_archive.playback_parts "
        "JOIN bacuri_archive.retained_blocks USING(site_id,block_id,recording_epoch_id,owner_generation) WHERE "
        "session_id=$1 "
        "AND (init_id=$2 OR media_id=$2) AND unavailable_reason IS NULL",
        {session, part});
    if (!rows.size())
        return {};
    return DeliveryPart{rows.get(0), rows.get(0, 1), rows.get(0, 2) == "t", std::stoll(rows.get(0, 3)),
                        std::stoll(rows.get(0, 4))};
}

std::vector<RecordedInterval> blocks_at(Database& db, const std::string& site, const std::string& point,
                                        const std::string& begin, const std::string& end)
{
    auto rows = db.exec(
        "SELECT begin_utc_us,end_utc_us,binding_revision,time_basis,uncertainty_us,unavailable_reason FROM "
        "bacuri_archive.retained_blocks "
        "WHERE site_id=$1 AND point_id=$2 AND int8range(begin_utc_us,end_utc_us,'[)') && "
        "int8range($3,$4,'[)') LIMIT 2",
        {site, point, begin, end});
    std::vector<RecordedInterval> blocks;
    blocks.reserve(static_cast<std::size_t>(rows.size()));
    for (int i = 0; i < rows.size(); ++i)
        blocks.push_back({std::stoll(rows.get(i)), std::stoll(rows.get(i, 1)), rows.get(i, 2), rows.get(i, 3),
                          rows.get(i, 4), rows.get(i, 5)});
    return blocks;
}

std::optional<std::int64_t> delivery_lifetime_ms(Database& db, const std::string& site, const std::string& point,
                                                 std::int64_t end)
{
    auto r = db.exec(R"sql(
      WITH lifetime AS (SELECT least((SELECT days FROM bacuri_archive.archive_retention WHERE site_id=$1),
        (SELECT days FROM bacuri_archive.point_retention WHERE site_id=$1 AND point_id=$3)) AS days),
      protection AS (SELECT max(bacuri_archive.hold_until(site_id,block_id)) AS expiry FROM bacuri_archive.blocks
          WHERE site_id=$1 AND point_id=$3 AND end_utc_us=$2 AND unavailable_reason IS NULL)
      SELECT least(5000,floor((greatest(expiry::numeric,$2::numeric+days*86400000000::bigint)-
          bacuri_archive.retention_now_us())/1000))::bigint FROM lifetime,protection
      WHERE ($2::bigint>bacuri_archive.point_cutoff($1,$3) OR expiry>bacuri_archive.retention_now_us())
        AND NOT EXISTS(SELECT FROM bacuri_archive.archive_recovery WHERE site_id=$1)
    )sql",
                     {site, std::to_string(end), point});
    if (!r.size())
        return {};
    return std::stoll(r.get(0));
}

std::string recovery_status(Database& db, const std::string& site)
{
    auto r = db.exec("SELECT reason FROM bacuri_archive.archive_recovery WHERE site_id=$1", {site});
    return r.size() ? r.get(0) : "available";
}

void set_retention(Database& db, const std::string& site, int days, int reserve)
{
    db.exec("BEGIN");
    change_retention(db, site, days, reserve);
    db.exec("COMMIT");
}
void change_retention(Database& db, const std::string& site, int days, int reserve)
{
    db.exec("SELECT pg_advisory_xact_lock(hashtextextended($1,1))", {site});
    db.exec("SELECT bacuri_archive.advance_cutoff($1)", {site});
    db.exec("INSERT INTO bacuri_archive.archive_retention(site_id,days,reserve_percent) VALUES($1,$2,$3) "
            "ON CONFLICT(site_id) DO UPDATE SET "
            "days=excluded.days,reserve_percent=excluded.reserve_percent,revision=archive_retention.revision+1",
            {site, std::to_string(days), std::to_string(reserve)});
    db.exec("SELECT bacuri_archive.advance_cutoff($1)", {site});
}

void await_verification(Database& db, const std::string& site)
{
    db.exec("INSERT INTO bacuri_archive.archive_recovery VALUES($1,'awaiting_verification') ON CONFLICT(site_id) "
            "DO UPDATE SET reason=excluded.reason",
            {site});
}
} // namespace bacuri::vms::control::recordings
