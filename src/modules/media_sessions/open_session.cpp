#include "modules/media_sessions/playback_operations.hpp"
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
Json live_video(postgresql::Database& db, const PointAccess& access, const Json& options, bool masked)
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
    free = std::min(free, options.value("bandwidth_kbps", 1000000u));
    const auto quality =
        options.value("quality", options.empty() ? std::string{"secondary"} : std::string{"auto"});
    const bool hevc = options.value("hevc", false);
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
        return Json::object();
    }
    const auto width = options.value("cell_width", 640u);
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
                Json result{{"profile", p.main() ? "main" : "secondary"},
                            {"source_codec", p.codec()},
                            {"codec", convert ? "h264" : p.codec()},
                            {"width", p.width()},
                            {"height", p.height()},
                            {"bitrate_kbps", bitrate},
                            {"converted", convert}};
                if (masked)
                    result["reason"] = "privacy_mask";
                else if (want != main || convert)
                    result["reason"] =
                        reason.empty() ? (convert ? "compatibility_copy" : "compatible_profile") : reason;
                return result;
            }
        }
    }
    throw Failure{copy_denied ? ErrorCode::video_transcoder_limit : ErrorCode::video_bandwidth_limit};
}

} // namespace
Json create(postgresql::Database& db, const Key& key, const PointAccess& access, const Request& req)
{
    auto body = object_body(req, {"mode", "interval", "binding_revision", "quality", "hevc", "accept_masked",
                                  "cell_width", "bandwidth_kbps"});
    const auto mode = string_field(body, "mode", 16);
    std::optional<Window> range;
    if (mode == "archive" && body.contains("interval")) {
        range = window(body.at("interval"));
    } else if (mode != "live" || body.contains("interval") || body.contains("binding_revision")) {
        throw Failure{ErrorCode::invalid_request};
    }
    auto options = body;
    for (const char* field : {"mode", "interval", "binding_revision"})
        options.erase(field);
    if ((range &&
         (options.contains("quality") || options.contains("cell_width") || options.contains("bandwidth_kbps"))) ||
        (options.contains("hevc") && !options.at("hevc").is_boolean()) ||
        (options.contains("accept_masked") && !options.at("accept_masked").is_boolean()) ||
        (options.contains("quality") && options.at("quality") != "auto" && options.at("quality") != "main" &&
         options.at("quality") != "secondary"))
        throw Failure{ErrorCode::invalid_request};
    for (auto field : {"cell_width", "bandwidth_kbps"})
        if (options.contains(field)) {
            const auto& value = options.at(field);
            if (!value.is_number_unsigned() || value < 1 ||
                value > (std::string_view(field) == "cell_width" ? 16384 : 1000000))
                throw Failure{ErrorCode::invalid_request};
        }
    const auto begin = range ? std::to_string(range->start) : std::string{};
    const auto end = range ? std::to_string(range->end) : std::string{};
    const auto mask =
        options.value("accept_masked", false) ? access::delivery_regions(db, access.site, access.point, mode) : "";
    if (!access::allowed(db, access.site, access.user, access.point, "view", mask.empty() ? mode : "", begin, end))
        throw Failure{ErrorCode::not_found};
    if (range)
        recordings::require_available(db, access.site);
    Json selection = options;
    selection["mode"] = mode;
    Json video = Json::object();
    std::optional<std::int64_t> binding;
    std::string revision;
    if (body.contains("binding_revision")) {
        revision = string_field(body, "binding_revision", 19);
        std::int64_t value{};
        auto [endptr, err] = std::from_chars(revision.data(), revision.data() + revision.size(), value);
        if (err != std::errc{} || endptr != revision.data() + revision.size() || value <= 0 ||
            revision.front() == '0')
            throw Failure{ErrorCode::invalid_request};
        binding = value;
        selection["binding_revision"] = revision;
    }
    if (range) {
        selection["interval"] = range->json();
    }
    const std::string request_key{req["Idempotency-Key"]};
    if (req.count("Idempotency-Key") != 1 || !canonical_id(request_key)) {
        throw Failure{ErrorCode::invalid_request};
    }
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
        Json reply{{"id", std::move(id)},
                   {"point_id", access.point},
                   {"selection", selection},
                   {"expires_at", std::move(expires)}};
        if (!video.empty() && !range) {
            reply["video"] = video;
            reply["video"].erase("mask");
        }
        return reply;
    };
    if (previous.size()) {
        if (previous.get(0, 4) == "t" || previous.get(0, 1) != begin || previous.get(0, 2) != end ||
            previous.get(0, 5) != revision || Json::parse(previous.get(0, 6)) != options) {
            throw Failure{ErrorCode::idempotency_conflict};
        }
        video = Json::parse(previous.get(0, 7));
        return result(previous.get(0), previous.get(0, 3));
    }
    if (db.exec("SELECT count(*)>=16 FROM bacuri_media_sessions.media_sessions WHERE browser=$1 AND NOT closed "
                "AND expires_at>now()",
                {access.browser})
            .get(0) == "t") {
        throw Failure{ErrorCode::rate_limited};
    }
    if (!range)
        video = live_video(db, access, options, !mask.empty());
    if (!mask.empty())
        video["mask"] = mask;
    auto session =
        db.exec("INSERT INTO "
                "bacuri_media_sessions.media_sessions(browser,site_id,point_id,begin_us,end_us,request_key,"
                "binding_revision,presentation_request,presentation,expires_at) "
                "VALUES ($1,$2,$3,nullif($4,'')::bigint,nullif($5,'')::bigint,$6,nullif($8,'')::bigint,$10,$11,"
                "least(now()+interval '5 minutes',$9::timestamptz)) RETURNING id,to_char(expires_at AT TIME ZONE "
                "'UTC',$7)",
                {access.browser, access.site, access.point, begin, end, request_key, expires_sql, revision,
                 access::browser_expiry(db, access.browser), options.dump(), video.dump()});
    if (!range) {
        return result(session.get(0), session.get(0, 1));
    }
    const bool bound =
        recordings::bind_playback_parts(db, session.get(0), access.site, access.point, begin, end, revision);
    if (!bound) {
        auto params = range->json();
        if (!mask.empty())
            params["presentation"] = "masked";
        const auto page = search(db, key, access, params, binding);
        const auto& items = page.at("items");
        if (std::ranges::any_of(items, [](const Json& entry) { return entry.at("state") == "pending_catalog"; })) {
            throw Failure{ErrorCode::pending_catalog};
        }
        if (!items.empty() &&
            std::ranges::all_of(items, [](const Json& entry) { return entry.at("state") == "gap"; })) {
            throw Failure{ErrorCode::recording_gap};
        }
        throw Failure{ErrorCode::media_unavailable};
    }
    if (!options.value("hevc", false) || !mask.empty()) {
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
