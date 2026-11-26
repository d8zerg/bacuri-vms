#include "modules/media_sessions/playback.hpp"
#include "application/control_api/playback_time.hpp"
#include "modules/access/operations.hpp"
#include "modules/recordings/operations.hpp"
#include "media/lease.hpp"
#include "platform/rpc_status.hpp"
#include <fstream>

namespace bacuri::vms::control
{
using namespace std::chrono_literals;
namespace
{
class RemotePart final : public MediaBody
{
  public:
    explicit RemotePart(const v1::ReadPartRequest& request)
        : lease_{request.scope()},
          stub_{v1::ArchiveDelivery::NewStub(media::dedicated_channel("BACURI_MEDIA_SOCKET"))}
    {
        lease_.deadline();
        context_.set_deadline(std::chrono::system_clock::now() + 30s);
        reader_ = stub_->ReadPart(&context_, request);
        if (!reader_->Read(&chunk_) || !chunk_.total_bytes()) {
            throw Failure{ErrorCode::media_service_unavailable};
        }
        size = chunk_.total_bytes();
    }
    RemotePart(const RemotePart&) = delete;
    RemotePart& operator=(const RemotePart&) = delete;
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

v1::AuthorizeDeliveryRequest scope(const postgresql::Rows& session, std::optional<Window> interval = {})
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

void copy_export_block(const std::string& session, const archive::BlockManifest& source,
                       const std::string& browser, const std::filesystem::path& destination)
{
    v1::ReadPartRequest q;
    auto* s = q.mutable_scope();
    s->set_export_source(true);
    s->set_media_session_id(session);
    s->set_browser_session_id(browser);
    s->mutable_point()->set_site_id(source.recording.block().site.to_string());
    s->mutable_point()->set_point_id(source.recording.binding().point.to_string());
    *s->mutable_archive()->mutable_start() = ProtoTime::MicrosecondsToTimestamp(source.begin_us());
    *s->mutable_archive()->mutable_end() = ProtoTime::MicrosecondsToTimestamp(source.end_us());
    q.set_block_id(source.recording.block().id.to_string());
    q.set_sha256(archive::digest_hex(source.sha256));
    q.set_complete_block_manifest(archive::manifest_json(source).dump());
    RemotePart stream{q};
    if (stream.size != static_cast<std::uint64_t>(source.size_bytes))
        throw Failure{ErrorCode::media_service_unavailable};
    std::ofstream out(destination, std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    for (auto remaining = stream.size; remaining;) {
        auto chunk = stream.next();
        if (chunk.bytes.empty() || chunk.bytes.size() > remaining)
            throw Failure{ErrorCode::media_service_unavailable};
        out.write(chunk.bytes.data(), static_cast<std::streamsize>(chunk.bytes.size()));
        remaining -= chunk.bytes.size();
    }
    out.close();
}
void Playback::session(const Request& req, Response& res, const std::string& id, std::string_view operation) const
{
    const bool closing = operation.empty();
    const bool offer = operation == "offer";
    allow(req, res, {closing ? http::verb::delete_ : offer ? http::verb::post : http::verb::get});
    postgresql::Database db;
    auto who = sessions_.authenticate(db, req, closing || offer, closing || offer);
    auto browser = fingerprint(key_, "session", who.cookie);
    if (closing) {
        db.exec("UPDATE bacuri_media_sessions.media_sessions SET closed=true WHERE id=$1 AND browser=$2",
                {id, browser});
        res.result(http::status::no_content);
        return;
    }
    db.exec("BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY");
    auto session = db.exec(
        "SELECT site_id,point_id,begin_us,end_us,closed OR "
        "expires_at<=clock_timestamp(),id,browser,presentation,presentation_request FROM "
        "bacuri_media_sessions.media_sessions WHERE id=$1 AND browser=$2 AND NOT presentation ? 'export_grant'",
        {id, browser});
    if (!session.size())
        throw Failure{ErrorCode::not_found};
    const auto surface = session.get(0, 2).empty() ? "live" : "archive";
    const auto video = Json::parse(session.get(0, 7));
    const std::string mask = video.value("mask", "");
    if (!mask.empty() && access::delivery_regions(db, session.get(0), session.get(0, 1), surface) != mask)
        throw Failure{ErrorCode::not_found};
    const auto expiry =
        access::browser_point_expiry(db, browser, session.get(0), session.get(0, 1), mask.empty() ? surface : "",
                                     session.get(0, 2), session.get(0, 3));
    if (!expiry)
        throw Failure{ErrorCode::not_found};
    const bool live = session.get(0, 2).empty();
    if (!live && recordings::recovery_status(db, session.get(0)) != "available")
        throw Failure{ErrorCode::not_found};
    if (session.get(0, 4) == "t" || db.exec("SELECT $1::timestamptz<=clock_timestamp()", {*expiry}).get(0) == "t")
        throw Failure{ErrorCode::media_session_closed};
    db.exec("COMMIT");
    if (live != offer) {
        throw Failure{ErrorCode::not_found};
    }
    if (offer) {
        if (req[http::field::content_type] != "application/sdp" || req.body().empty() ||
            req.body().size() > 65536 || req.body().contains('\0')) {
            throw Failure{ErrorCode::invalid_request};
        }
        if (db.exec("SELECT live_negotiated FROM bacuri_media_sessions.media_sessions WHERE id=$1", {id}).get(0) ==
            "t") {
            throw Failure{ErrorCode::idempotency_conflict};
        }
        auto stub = v1::LiveDelivery::NewStub(media::local_channel("BACURI_MEDIA_SOCKET"));
        grpc::ClientContext rpc;
        rpc.set_deadline(std::chrono::system_clock::now() + 6s);
        v1::LiveOffer input;
        *input.mutable_scope() = scope(session);
        input.set_sdp(req.body());
        v1::LiveAnswer answer;
        auto status = stub->Negotiate(&rpc, input, &answer);
        if (const auto cause = platform::rpc_problem(status))
            throw Failure{*cause};
        // Older peers and transport errors supply only the gRPC status.
        switch (status.error_code()) {
        case grpc::StatusCode::OK:
            break;
        case grpc::StatusCode::INVALID_ARGUMENT:
            throw Failure{ErrorCode::invalid_request};
        case grpc::StatusCode::RESOURCE_EXHAUSTED:
            throw Failure{ErrorCode::rate_limited};
        case grpc::StatusCode::ALREADY_EXISTS:
            throw Failure{ErrorCode::idempotency_conflict};
        default:
            throw Failure{ErrorCode::media_service_unavailable};
        }
        if (!db.exec("UPDATE bacuri_media_sessions.media_sessions SET live_negotiated=true WHERE id=$1 AND NOT "
                     "live_negotiated AND NOT "
                     "closed AND expires_at>clock_timestamp() RETURNING id",
                     {id})
                 .size()) {
            throw Failure{ErrorCode::media_session_closed};
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
        throw Failure{ErrorCode::not_found};
    }
    auto part = recordings::playback_part(db, id, std::string{operation.substr(6)});
    if (!part) {
        throw Failure{ErrorCode::not_found};
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
} // namespace bacuri::vms::control
