#include "application/control_api/http_json.hpp"
#include "domain/image_region_json.hpp"
#include "modules/media_sessions/playback_json.hpp"
#include "application/control_api/playback_time.hpp"
#include "modules/access/operations.hpp"
#include "modules/recordings/operations.hpp"
#include "media/lease.hpp"

namespace bacuri::vms::control::playback
{
using namespace std::chrono_literals;
namespace
{
constexpr auto expires_sql = R"(YYYY-MM-DD"T"HH24:MI:SS.US"Z")";

unsigned video_limit(const char* name, unsigned fallback)
{
    const auto raw = optional_setting(name);
    if (!raw)
        return fallback;
    const std::string_view value{*raw};
    unsigned n{};
    const auto [last, error] = std::from_chars(value.begin(), value.end(), n);
    if (error != std::errc{} || last != value.end() || n > 1000000)
        throw Failure{ErrorCode::media_service_unavailable};
    return n;
}
std::optional<VideoProfile> live_video(postgresql::Database& db, const PointAccess& access,
                                       const PresentationOptions& options, bool masked)
{
    const auto usage =
        db.exec("SELECT coalesce(sum(coalesce((presentation->>'bitrate_kbps')::int,768)) FILTER(WHERE begin_us IS "
                "NULL),0),count(*) FILTER(WHERE presentation->>'converted'='true'),count(*) FILTER(WHERE begin_us "
                "IS NULL) FROM "
                "bacuri_media_sessions.media_sessions WHERE NOT closed AND expires_at>now()");
    if (std::stoul(usage.get(0, 2)) >= 16)
        throw Failure{ErrorCode::video_session_limit};
    const auto used = std::stoul(usage.get(0));
    auto budget = video_limit("BACURI_VIDEO_BUDGET_KBPS", 64000);
    auto free = used < budget ? budget - static_cast<unsigned>(used) : 0;
    free = std::min(free, options.bandwidth_kbps.value_or(1000000u));
    const auto quality =
        options.quality.value_or(options.empty() ? std::string{"secondary"} : std::string{"auto"});
    const bool hevc = options.hevc.value_or(false);
    v1::PointKey query;
    query.set_site_id(access.site);
    query.set_point_id(access.point);
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + 1s);
    v1::VideoProfiles available;
    if (!v1::RecorderControl::NewStub(media::local_channel("BACURI_SOCKET"))
             ->GetVideoProfiles(&ctx, query, &available)
             .ok() ||
        available.profiles().empty()) {
        if (!options.empty())
            throw Failure{ErrorCode::video_source_unavailable};
        if (free < 768)
            throw Failure{ErrorCode::video_bandwidth_limit};
        return {};
    }
    const auto width = options.cell_width.value_or(640u);
    const bool main =
        quality == "main" || (quality == "auto" && std::ranges::none_of(available.profiles(), [&](const auto& p) {
                                  return !p.main() && p.width() >= width;
                              }));
    const bool can_convert = std::stoul(usage.get(0, 1)) < video_limit("BACURI_VIDEO_TRANSCODERS", 1);
    bool copy_denied = false;
    std::string reason;
    for (bool convert : {false, true}) {
        for (bool want : {main, !main}) {
            for (const auto& p : available.profiles()) {
                if (p.main() != want || (p.codec() != "h264" && p.codec() != "h265"))
                    continue;
                const bool needs_copy = masked || (p.codec() == "h265" && !hevc);
                if (needs_copy != convert)
                    continue;
                if (convert && !can_convert) {
                    copy_denied = true;
                    continue;
                }
                const unsigned bitrate = convert ? 2000 : p.bitrate_kbps();
                if (bitrate > free) {
                    reason = "bandwidth";
                    continue;
                }
                VideoProfile result{p.main() ? "main" : "secondary",
                                    p.codec(),
                                    convert ? "h264" : p.codec(),
                                    p.width(),
                                    p.height(),
                                    bitrate,
                                    convert,
                                    {}};
                if (masked)
                    result.reason = "privacy_mask";
                else if (want != main || convert)
                    result.reason =
                        reason.empty() ? (convert ? "compatibility_copy" : "compatible_profile") : reason;
                return result;
            }
        }
    }
    throw Failure{copy_denied ? ErrorCode::video_transcoder_limit : ErrorCode::video_bandwidth_limit};
}

} // namespace
MediaSession create(postgresql::Database& db, const Key& key, const PointAccess& access,
                    const SessionSelection& selection, const std::string& request_key)
{
    const auto& [mode, range, binding, options] = selection;
    const auto begin = range ? std::to_string(range->start) : std::string{};
    const auto end = range ? std::to_string(range->end) : std::string{};
    const auto mask = options.accept_masked.value_or(false)
                          ? access::delivery_regions(db, access.site, access.point, mode)
                          : domain::ImageRegions{};
    if (!access::allowed(db, access.site, access.user, access.point, "view", mask.empty() ? mode : "", begin, end))
        throw Failure{ErrorCode::not_found};
    if (range)
        recordings::require_available(db, access.site);
    const auto revision = binding ? std::to_string(*binding) : std::string{};
    std::optional<VideoProfile> profile;
    Json video = Json::object();
    db.exec("SELECT pg_advisory_xact_lock(486324029)");
    db.exec("UPDATE bacuri_media_sessions.media_sessions SET request_key=NULL WHERE browser=$1 AND request_key=$2 "
            "AND expires_at<=now()",
            {access.browser, request_key});
    auto previous =
        db.exec("SELECT id,begin_us,end_us,to_char(expires_at AT TIME ZONE "
                "'UTC',$5),closed,binding_revision,presentation_request,presentation "
                "FROM bacuri_media_sessions.media_sessions WHERE browser=$1 AND site_id=$2 AND point_id=$3 AND "
                "request_key=$4",
                {access.browser, access.site, access.point, request_key, expires_sql});
    auto result = [&](std::string id, std::string expires) {
        return MediaSession{std::move(id), access.point, std::move(expires), selection, std::move(profile)};
    };
    if (previous.size()) {
        if (previous.get(0, 4) == "t" || previous.get(0, 1) != begin || previous.get(0, 2) != end ||
            previous.get(0, 5) != revision ||
            Json::parse(previous.get(0, 6)).get<PresentationOptions>() != options) {
            throw Failure{ErrorCode::idempotency_conflict};
        }
        const auto previous_video = Json::parse(previous.get(0, 7));
        if (!range && previous_video.contains("profile"))
            profile = previous_video.get<VideoProfile>();
        return result(previous.get(0), previous.get(0, 3));
    }
    if (db.exec("SELECT count(*)>=16 FROM bacuri_media_sessions.media_sessions WHERE browser=$1 AND NOT closed "
                "AND expires_at>now()",
                {access.browser})
            .get(0) == "t") {
        throw Failure{ErrorCode::rate_limited};
    }
    if (!range) {
        profile = live_video(db, access, options, !mask.empty());
        if (profile)
            video = *profile;
    }
    if (!mask.empty())
        video["mask"] = domain::encode_regions(mask);
    auto session =
        db.exec("INSERT INTO "
                "bacuri_media_sessions.media_sessions(browser,site_id,point_id,begin_us,end_us,request_key,"
                "binding_revision,presentation_request,presentation,expires_at) "
                "VALUES ($1,$2,$3,nullif($4,'')::bigint,nullif($5,'')::bigint,$6,nullif($8,'')::bigint,$10,$11,"
                "least(now()+interval '5 minutes',$9::timestamptz)) RETURNING id,to_char(expires_at AT TIME ZONE "
                "'UTC',$7)",
                {access.browser, access.site, access.point, begin, end, request_key, expires_sql, revision,
                 access::browser_expiry(db, access.browser), Json(options).dump(), video.dump()});
    if (!range) {
        return result(session.get(0), session.get(0, 1));
    }
    const bool bound =
        recordings::bind_playback_parts(db, session.get(0), access.site, access.point, begin, end, revision);
    if (!bound) {
        const auto page = search(db, key, access, {*range, {}, !mask.empty()}, binding);
        const auto& items = page.items;
        if (std::ranges::any_of(items, [](const auto& entry) { return entry.state == "pending_catalog"; })) {
            throw Failure{ErrorCode::pending_catalog};
        }
        if (!items.empty() && std::ranges::all_of(items, [](const auto& entry) { return entry.state == "gap"; })) {
            throw Failure{ErrorCode::recording_gap};
        }
        throw Failure{ErrorCode::media_unavailable};
    }
    if (!options.hevc.value_or(false) || !mask.empty()) {
        const auto parts = recordings::playback_manifest(db, session.get(0));
        if (!mask.empty() || std::ranges::any_of(parts, [](const auto& p) { return p.codec == "h265"; })) {
            // A repeated request for a session that already converts does not count against itself.
            if (std::stoul(db.exec("SELECT count(*) FROM bacuri_media_sessions.media_sessions WHERE "
                                   "presentation->>'converted'='true' AND NOT closed AND expires_at>now() "
                                   "AND id<>$1",
                                   {session.get(0)})
                               .get(0)) >= video_limit("BACURI_VIDEO_TRANSCODERS", 1))
                throw Failure{ErrorCode::video_transcoder_limit};
            video["converted"] = true;
            db.exec("UPDATE bacuri_media_sessions.media_sessions SET presentation=$2 WHERE id=$1",
                    {session.get(0), video.dump()});
        }
    }
    return result(session.get(0), session.get(0, 1));
}
} // namespace bacuri::vms::control::playback
