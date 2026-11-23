#include "control/inventory/operations.hpp"
#include "control/events/operations.hpp"
#include "control/access/operations.hpp"
#include "control/recordings/operations.hpp"
#include "control/playback.hpp"
#include "control/playback_time.hpp"
#include "domain/time.hpp"
#include "media/lease.hpp"
#include <charconv>
#include <fstream>

namespace bacuri::vms::control
{
namespace
{
using namespace std::chrono_literals;

Json query_parameters(std::string_view target)
{
    Json fields = Json::object();
    const auto separator = target.find('?');
    if (separator == target.npos) {
        return fields;
    }
    target.remove_prefix(separator + 1);
    while (!target.empty()) {
        auto end = target.find('&');
        auto pair = target.substr(0, end);
        auto eq = pair.find('=');
        auto key = pair.substr(0, eq);
        if (eq == pair.npos || (key != "start" && key != "end" && key != "cursor" && key != "presentation") ||
            fields.contains(key)) {
            throw Failure{400, "invalid_request"};
        }
        const std::unique_ptr<char, decltype(&g_free)> value{
            g_uri_unescape_segment(pair.data() + eq + 1, pair.data() + pair.size(), nullptr), g_free};
        if (!value) {
            throw Failure{400, "invalid_request"};
        }
        fields[key] = value.get();
        if (end == target.npos) {
            break;
        }
        target.remove_prefix(end + 1);
    }
    return fields;
}

struct PointAccess {
    std::string site, point, browser, role, user;
};

Json search(Database& db, const Key& key, const PointAccess& access, Json params,
            std::optional<std::int64_t> binding = {})
{
    const auto cursor = params.value("cursor", std::string{});
    const bool continued = params.erase("cursor") != 0;
    const auto presentation = params.value("presentation", "");
    if (!presentation.empty() && presentation != "masked")
        throw Failure{400, "invalid_request"};
    params.erase("presentation");
    const auto bounds = window(params);
    const auto mask =
        presentation == "masked" ? access::delivery_regions(db, access.site, access.point, "archive") : "";
    if (!access::allowed(db, access.site, access.user, access.point, "view", mask.empty() ? "archive" : "",
                         std::to_string(bounds.start), std::to_string(bounds.end)))
        throw Failure{404, "not_found"};
    recordings::require_available(db, access.site);
    auto position = bounds.start;
    const auto context =
        Json::array({"archive", access.browser, access.role, access.site, access.point, bounds.start, bounds.end})
            .dump();
    if (continued) {
        try {
            if (cursor.empty() || cursor.size() > 1024) {
                throw std::invalid_argument("Invalid cursor");
            }
            auto plain = unseal("k1:" + cursor, key, context);
            position = std::stoll(plain);
            if (position <= bounds.start || position >= bounds.end) {
                throw std::invalid_argument("Invalid cursor");
            }
        } catch (const std::exception&) {
            throw Failure{400, "invalid_request"};
        }
    }
    auto recorder = v1::RecorderControl::NewStub(media::local_channel("BACURI_SOCKET"));
    grpc::ClientContext rpc;
    rpc.set_deadline(std::chrono::system_clock::now() + 1s);
    v1::PendingQuery q;
    q.mutable_point()->set_site_id(access.site);
    q.mutable_point()->set_point_id(access.point);
    q.set_begin_us(position);
    q.set_end_us(bounds.end);
    q.set_retention_cutoff_us(recordings::retention_cutoff(db, access.site, access.point));
    if (binding) {
        q.set_binding_revision(*binding);
    }
    v1::PendingIntervals pending;
    Json local = Json::array();
    auto horizon = bounds.end;
    if (recorder->ListPending(&rpc, q, &pending).ok()) {
        for (const auto& interval : pending.intervals()) {
            if (local.size() == 1000 && interval.begin_us() > position) {
                horizon = interval.begin_us();
                break;
            }
            local.push_back({{"a", interval.begin_us()}, {"b", interval.end_us()}});
        }
    }
    auto rows =
        recordings::coverage(db, access.site, access.point, std::to_string(position), std::to_string(horizon),
                             local.dump(), binding ? std::to_string(*binding) : "");
    auto items = Json::array();
    for (const auto& row : rows) {
        if (items.size() == 1000)
            break;
        Window span{row.begin, row.end};
        Json entry{{"interval", span.json()}, {"state", row.state}};
        if (auto basis = row.time_basis; !basis.empty()) {
            entry["time_basis"] = basis;
        }
        if (auto uncertainty = row.uncertainty; !uncertainty.empty()) {
            entry["uncertainty_us"] = uncertainty;
        }
        if (auto reason = row.reason; !reason.empty()) {
            entry["reason"] = reason;
        }
        items.push_back(std::move(entry));
        position = span.end;
    }
    Json page{{"items", std::move(items)}};
    if (position < bounds.end) {
        page["next_cursor"] = seal(key, context, std::to_string(position)).substr(3);
    }
    return page;
}

constexpr auto expires_sql = "YYYY-MM-DD\"T\"HH24:MI:SS.US\"Z\"";

unsigned video_limit(const char* name, unsigned fallback)
{
    const char* raw = std::getenv(name);
    if (!raw)
        return fallback;
    const std::string_view value{raw};
    unsigned n{};
    const auto [last, error] = std::from_chars(value.begin(), value.end(), n);
    if (error != std::errc{} || last != value.end() || n > 1000000)
        throw Failure{503, "media_unavailable"};
    return n;
}
Json live_video(Database& db, const PointAccess& access, const Json& options, bool masked)
{
    const auto usage =
        db.exec("SELECT coalesce(sum(coalesce((presentation->>'bitrate_kbps')::int,768)) FILTER(WHERE begin_us IS "
                "NULL),0),count(*) FILTER(WHERE presentation->>'converted'='true'),count(*) FILTER(WHERE begin_us "
                "IS NULL) FROM "
                "bacuri_media_sessions.media_sessions WHERE NOT closed AND expires_at>now()");
    if (std::stoul(usage.get(0, 2)) >= 16)
        throw Failure{429, "video_session_limit"};
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
            throw Failure{503, "video_source_unavailable"};
        if (free < 768)
            throw Failure{429, "video_bandwidth_limit"};
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
    throw Failure{429, copy_denied ? "video_transcoder_limit" : "video_bandwidth_limit"};
}

Json create(Database& db, const Key& key, const PointAccess& access, const Request& req)
{
    auto body = object_body(req, {"mode", "interval", "binding_revision", "quality", "hevc", "accept_masked",
                                  "cell_width", "bandwidth_kbps"});
    const auto mode = string_field(body, "mode", 16);
    std::optional<Window> range;
    if (mode == "archive" && body.contains("interval")) {
        range = window(body.at("interval"));
    } else if (mode != "live" || body.contains("interval") || body.contains("binding_revision")) {
        throw Failure{400, "invalid_request"};
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
        throw Failure{400, "invalid_request"};
    for (auto field : {"cell_width", "bandwidth_kbps"})
        if (options.contains(field)) {
            const auto& value = options.at(field);
            if (!value.is_number_unsigned() || value < 1 ||
                value > (std::string_view(field) == "cell_width" ? 16384 : 1000000))
                throw Failure{400, "invalid_request"};
        }
    const auto begin = range ? std::to_string(range->start) : std::string{};
    const auto end = range ? std::to_string(range->end) : std::string{};
    const auto mask =
        options.value("accept_masked", false) ? access::delivery_regions(db, access.site, access.point, mode) : "";
    if (!access::allowed(db, access.site, access.user, access.point, "view", mask.empty() ? mode : "", begin, end))
        throw Failure{404, "not_found"};
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
            throw Failure{400, "invalid_request"};
        binding = value;
        selection["binding_revision"] = revision;
    }
    if (range) {
        selection["interval"] = range->json();
    }
    const std::string request_key{req["Idempotency-Key"]};
    if (req.count("Idempotency-Key") != 1 || !canonical_id(request_key)) {
        throw Failure{400, "invalid_request"};
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
            throw Failure{409, "idempotency_conflict"};
        }
        video = Json::parse(previous.get(0, 7));
        return result(previous.get(0), previous.get(0, 3));
    }
    if (db.exec("SELECT count(*)>=16 FROM bacuri_media_sessions.media_sessions WHERE browser=$1 AND NOT closed "
                "AND expires_at>now()",
                {access.browser})
            .get(0) == "t") {
        throw Failure{429, "rate_limited"};
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
            throw Failure{409, "pending_catalog"};
        }
        if (!items.empty() &&
            std::ranges::all_of(items, [](const Json& entry) { return entry.at("state") == "gap"; })) {
            throw Failure{409, "recording_gap"};
        }
        throw Failure{409, "media_unavailable"};
    }
    if (!options.value("hevc", false) || !mask.empty()) {
        const auto parts = recordings::playback_manifest(db, session.get(0));
        if (!mask.empty() || std::ranges::any_of(parts, [](const auto& p) { return p.codec == "h265"; })) {
            if (std::stoul(db.exec("SELECT count(*) FROM bacuri_media_sessions.media_sessions WHERE "
                                   "presentation->>'converted'='true' AND NOT closed AND expires_at>now()")
                               .get(0)) >= video_limit("BACURI_VIDEO_TRANSCODERS", 1))
                throw Failure{429, "video_transcoder_limit"};
            video["converted"] = true;
            db.exec("UPDATE bacuri_media_sessions.media_sessions SET presentation=$2 WHERE id=$1",
                    {session.get(0), video.dump()});
        }
    }
    return result(session.get(0), session.get(0, 1));
}

class RemotePart final : public MediaBody
{
  public:
    explicit RemotePart(const v1::ReadPartRequest& request)
        : lease_{request.scope()}, stub_{v1::ArchiveDelivery::NewStub(media::local_channel("BACURI_MEDIA_SOCKET"))}
    {
        lease_.deadline();
        context_.set_deadline(std::chrono::system_clock::now() + 30s);
        reader_ = stub_->ReadPart(&context_, request);
        if (!reader_->Read(&chunk_) || !chunk_.total_bytes()) {
            throw Failure{503, "media_unavailable"};
        }
        size = chunk_.total_bytes();
    }
    ~RemotePart() override
    {
        context_.TryCancel();
    }
    MediaChunk next() override
    {
        if (chunk_.data().empty() && !reader_->Read(&chunk_)) {
            throw std::runtime_error("Archive stream ended early");
        }
        if (chunk_.data().size() > media::chunk_size) {
            throw std::runtime_error("Oversized media chunk");
        }
        auto deadline = lease_.deadline();
        MediaChunk result{{}, deadline};
        result.bytes.swap(*chunk_.mutable_data());
        return result;
    }
    std::uint64_t size{};

  private:
    media::Lease lease_;
    grpc::ClientContext context_;
    std::unique_ptr<v1::ArchiveDelivery::Stub> stub_;
    std::unique_ptr<grpc::ClientReader<v1::PartChunk>> reader_;
    v1::PartChunk chunk_;
};

v1::AuthorizeDeliveryRequest scope(const Rows& session, std::optional<Window> interval = {})
{
    v1::AuthorizeDeliveryRequest q;
    q.set_media_session_id(session.get(0, 5));
    q.set_browser_session_id(session.get(0, 6));
    q.mutable_point()->set_site_id(session.get(0));
    q.mutable_point()->set_point_id(session.get(0, 1));
    const auto video = Json::parse(session.get(0, 7));
    q.set_privacy_regions(video.value("mask", ""));
    if (interval) {
        q.set_archive_h264(video.value("converted", false));
        *q.mutable_archive()->mutable_start() = ProtoTime::MicrosecondsToTimestamp(interval->start);
        *q.mutable_archive()->mutable_end() = ProtoTime::MicrosecondsToTimestamp(interval->end);
    } else {
        auto* live = q.mutable_live();
        live->set_main(video.value("profile", "") == "main");
        live->set_source_codec(video.value("source_codec", "h264"));
        live->set_output_codec(video.value("codec", "h264"));
    }
    return q;
}
} // namespace

void copy_export_block(const std::string& session, const Json& source, const std::string& browser,
                       const std::filesystem::path& destination)
{
    v1::ReadPartRequest q;
    auto* s = q.mutable_scope();
    s->set_media_session_id(session);
    s->set_browser_session_id(browser);
    s->mutable_point()->set_site_id(source.at("site_id").get<std::string>());
    s->mutable_point()->set_point_id(source.at("point_id").get<std::string>());
    *s->mutable_archive()->mutable_start() = ProtoTime::MicrosecondsToTimestamp(source.at("begin_utc_us"));
    *s->mutable_archive()->mutable_end() = ProtoTime::MicrosecondsToTimestamp(source.at("end_utc_us"));
    q.set_block_id(source.at("block_id").get<std::string>());
    q.set_sha256(source.at("media").at("sha256").get<std::string>());
    q.set_complete_block_manifest(source.dump());
    RemotePart stream{q};
    if (stream.size != source.at("media").at("size_bytes").get<std::uint64_t>())
        throw Failure{503, "media_unavailable"};
    std::ofstream out(destination, std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    for (auto remaining = stream.size; remaining;) {
        auto chunk = stream.next();
        if (chunk.bytes.empty() || chunk.bytes.size() > remaining)
            throw Failure{503, "media_unavailable"};
        out.write(chunk.bytes.data(), static_cast<std::streamsize>(chunk.bytes.size()));
        remaining -= chunk.bytes.size();
    }
    out.close();
}
void Playback::point(const Request& req, Response& res, const std::string& site, const std::string& point,
                     bool searching) const
{
    allow(req, res, {searching ? http::verb::get : http::verb::post});
    Database db;
    db.exec("BEGIN");
    auto who = sessions_.authenticate(db, req, !searching);
    if (!inventory::point_exists(db, site, point))
        throw Failure{404, "not_found"};
    PointAccess access{site, point, fingerprint(key_, "session", who.cookie),
                       access::site_role(db, site, who.user, access::RoleLock::read), who.user};
    if (searching) {
        respond(res, search(db, key_, access, query_parameters(std::string_view{req.target()})));
    } else {
        respond(res, create(db, key_, access, req), 201);
    }
    db.exec("COMMIT");
}

void Playback::session(const Request& req, Response& res, const std::string& id, std::string_view operation) const
{
    const bool closing = operation.empty();
    const bool offer = operation == "offer";
    allow(req, res, {closing ? http::verb::delete_ : offer ? http::verb::post : http::verb::get});
    Database db;
    auto who = sessions_.authenticate(db, req, closing || offer, closing || offer);
    auto browser = fingerprint(key_, "session", who.cookie);
    if (closing) {
        db.exec("UPDATE bacuri_media_sessions.media_sessions SET closed=true WHERE id=$1 AND browser=$2",
                {id, browser});
        res.result(http::status::no_content);
        return;
    }
    db.exec("BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY");
    auto session = db.exec("SELECT site_id,point_id,begin_us,end_us,closed OR "
                           "expires_at<=clock_timestamp(),id,browser,presentation,presentation_request FROM "
                           "bacuri_media_sessions.media_sessions WHERE id=$1 AND browser=$2",
                           {id, browser});
    if (!session.size())
        throw Failure{404, "not_found"};
    const auto surface = session.get(0, 2).empty() ? "live" : "archive";
    const auto video = Json::parse(session.get(0, 7));
    const std::string mask = video.value("mask", "");
    if (!mask.empty() && access::delivery_regions(db, session.get(0), session.get(0, 1), surface) != mask)
        throw Failure{404, "not_found"};
    const auto expiry =
        access::browser_point_expiry(db, browser, session.get(0), session.get(0, 1), mask.empty() ? surface : "",
                                     session.get(0, 2), session.get(0, 3));
    if (!expiry)
        throw Failure{404, "not_found"};
    const bool live = session.get(0, 2).empty();
    if (!live && recordings::recovery_status(db, session.get(0)) != "available")
        throw Failure{404, "not_found"};
    if (session.get(0, 4) == "t" || db.exec("SELECT $1::timestamptz<=clock_timestamp()", {*expiry}).get(0) == "t")
        throw Failure{410, "media_session_closed"};
    db.exec("COMMIT");
    if (live != offer) {
        throw Failure{404, "not_found"};
    }
    if (offer) {
        if (req[http::field::content_type] != "application/sdp" || req.body().empty() ||
            req.body().size() > 65536 || req.body().contains('\0')) {
            throw Failure{400, "invalid_request"};
        }
        if (db.exec("SELECT live_negotiated FROM bacuri_media_sessions.media_sessions WHERE id=$1", {id}).get(0) ==
            "t") {
            throw Failure{409, "idempotency_conflict"};
        }
        auto stub = v1::LiveDelivery::NewStub(media::local_channel("BACURI_MEDIA_SOCKET"));
        grpc::ClientContext rpc;
        rpc.set_deadline(std::chrono::system_clock::now() + 6s);
        v1::LiveOffer input;
        *input.mutable_scope() = scope(session);
        input.set_sdp(req.body());
        v1::LiveAnswer answer;
        auto status = stub->Negotiate(&rpc, input, &answer);
        switch (status.error_code()) {
        case grpc::StatusCode::OK:
            break;
        case grpc::StatusCode::INVALID_ARGUMENT:
            throw Failure{400, "invalid_request"};
        case grpc::StatusCode::RESOURCE_EXHAUSTED:
            throw Failure{429, "rate_limited"};
        case grpc::StatusCode::ALREADY_EXISTS:
            throw Failure{409, "idempotency_conflict"};
        default:
            throw Failure{503, "media_unavailable"};
        }
        if (!db.exec("UPDATE bacuri_media_sessions.media_sessions SET live_negotiated=true WHERE id=$1 AND NOT "
                     "live_negotiated AND NOT "
                     "closed AND expires_at>clock_timestamp() RETURNING id",
                     {id})
                 .size()) {
            throw Failure{410, "media_session_closed"};
        }
        res.result(http::status::ok);
        res.set(http::field::content_type, "application/sdp");
        res.body() = std::move(*answer.mutable_sdp());
        return;
    }
    if (operation == "manifest") {
        media::Lease lease{scope(session, Window{std::stoll(session.get(0, 2)), std::stoll(session.get(0, 3))})};
        auto rows = recordings::playback_manifest(db, id);
        auto parts = Json::array();
        for (const auto& row : rows) {
            parts.push_back({{"init_id", row.initialization},
                             {"media_id", row.media},
                             {"source_codec", row.codec},
                             {"codec", video.value("converted", false) ? "h264" : row.codec},
                             {"interval", Window{row.begin, row.end}.json()}});
        }
        lease.deadline();
        respond(res, {{"mime_type", "video/mp4"}, {"parts", std::move(parts)}});
        return;
    }
    if (!operation.starts_with("parts/") || !canonical_id(operation.substr(6))) {
        throw Failure{404, "not_found"};
    }
    auto part = recordings::playback_part(db, id, std::string{operation.substr(6)});
    if (!part) {
        throw Failure{404, "not_found"};
    }
    v1::ReadPartRequest q;
    *q.mutable_scope() = scope(session, Window{part->begin, part->end});
    q.set_block_id(part->block);
    q.set_sha256(part->sha256);
    q.set_initialization(part->initialization);
    auto body = std::make_shared<RemotePart>(q);
    res.result(http::status::ok);
    res.set(http::field::content_type, "video/mp4");
    res.media_size = body->size;
    res.media = std::move(body);
}
void Playback::alert_archive(const Request& req, Response& res, const std::string& site,
                             const std::string& id) const
{
    allow(req, res, {http::verb::get});
    Database db;
    db.exec("BEGIN");
    auto who = sessions_.authenticate(db, req, false);
    auto anchor = events::archive_anchor(db, site, id);
    if (!anchor)
        throw Failure{404, "not_found"};
    const auto& point = anchor->point;
    auto role = access::role_for_existing_point(db, site, point, who.user);
    if (!access::allowed(db, site, who.user, point, "view", "archive"))
        throw Failure{404, "not_found"};
    recordings::require_available(db, site);
    const auto time = anchor->time;
    Json link{{"point_id", point}, {"state", "uncertain_time"}, {"archive_interval", alert_window(time).json()}};
    if (anchor->basis == "source_clock" && std::stoll(anchor->uncertainty) <= alert_context_us) {
        const auto uncertainty = std::stoll(anchor->uncertainty);
        Window instant{time - uncertainty, time + uncertainty + 1};
        auto blocks =
            recordings::blocks_at(db, site, point, std::to_string(instant.start), std::to_string(instant.end));
        if (blocks.size() == 1 && blocks.front().revision == anchor->revision &&
            blocks.front().basis == "source_clock" && blocks.front().unavailable_reason.empty()) {
            Window block{blocks.front().begin, blocks.front().end};
            auto error = std::stoll(blocks.front().uncertainty);
            using domain::Microseconds;
            using domain::UtcTime;
            auto interval =
                domain::UtcInterval::make(UtcTime{Microseconds{block.start}}, UtcTime{Microseconds{block.end}});
            if (interval && error <= Microseconds::max().count() - uncertainty &&
                interval->contains(UtcTime{Microseconds{time}}, Microseconds{uncertainty + error})) {
                link["state"] = "ready";
                link["interval"] = block.json();
                link["position"] = ProtoTime::ToString(ProtoTime::MicrosecondsToTimestamp(time));
            }
        } else if (blocks.empty() || (blocks.size() == 1 && !blocks.front().unavailable_reason.empty())) {
            auto page = search(db, key_, {site, point, fingerprint(key_, "session", who.cookie), role, who.user},
                               instant.json(), std::stoll(anchor->revision));
            const auto& items = page.at("items");
            if (items.size() == 1 && items[0].at("state") != "recorded") {
                link["state"] = items[0].at("state");
                if (items[0].contains("reason")) {
                    link["reason"] = items[0].at("reason");
                }
            }
        }
    }
    db.exec("COMMIT");
    respond(res, link);
}
} // namespace bacuri::vms::control
