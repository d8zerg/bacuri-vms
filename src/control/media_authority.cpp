#include "control/access/operations.hpp"
#include "control/recordings/operations.hpp"
#include "control/media_authority.hpp"
#include "control/playback_time.hpp"
#include <limits>
#include <grpcpp/grpcpp.h>
#include <sys/stat.h>

namespace bacuri::vms::control
{
MediaAuthority::MediaAuthority()
{
    const auto path = setting("BACURI_AUTH_SOCKET");
    grpc::ServerBuilder builder;
    builder.AddListeningPort("unix:" + path, grpc::experimental::LocalServerCredentials(UDS));
    builder.RegisterService(this);
    server_ = builder.BuildAndStart();
    if (!server_ || chmod(path.c_str(), 0660)) {
        throw std::runtime_error("Cannot open media authorization socket");
    }
}
MediaAuthority::~MediaAuthority()
{
    server_->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds{2});
    server_->Wait();
}
grpc::Status MediaAuthority::AuthorizeDelivery(grpc::ServerContext*, const v1::AuthorizeDeliveryRequest* q,
                                               v1::DeliveryLease* lease)
{
    try {
        if (!canonical_id(q->media_session_id()) || !canonical_id(q->point().site_id()) ||
            !canonical_id(q->point().point_id()) ||
            !(q->has_live() || (q->has_archive() && q->archive().has_start() && q->archive().has_end()))) {
            return {grpc::StatusCode::PERMISSION_DENIED, "Invalid scope"};
        }
        const Window bounds{utc_us(q->archive().start()), utc_us(q->archive().end())};
        Database db;
        db.exec("BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY");
        if (q->export_source()) {
            if (!q->has_archive() || q->archive_h264() || !q->privacy_regions().empty())
                return {grpc::StatusCode::PERMISSION_DENIED, "Invalid export source"};
            const auto source =
                db.exec("SELECT presentation FROM bacuri_media_sessions.media_sessions WHERE id=$1 AND browser=$2 "
                        "AND site_id=$3 AND point_id=$4 "
                        "AND NOT closed AND expires_at>now() AND begin_us<=$5 AND end_us>=$6 AND presentation ? "
                        "'export_grant'",
                        {q->media_session_id(), q->browser_session_id(), q->point().site_id(),
                         q->point().point_id(), std::to_string(bounds.start), std::to_string(bounds.end)});
            if (!source.size())
                return {grpc::StatusCode::PERMISSION_DENIED, "Unknown export session"};
            auto p = Json::parse(source.get(0));
            auto grant = access::export_lease(db, q->browser_session_id(), p.at("export_grant"),
                                              q->point().point_id(), p.at("begin"), p.at("end"),
                                              p.at("derivative"), p.at("regions"), p.at("sound"));
            if (!grant || grant->milliseconds <= 0)
                return {grpc::StatusCode::PERMISSION_DENIED, "Export permission expired"};
            db.exec("COMMIT");
            lease->set_valid_for_ms(static_cast<std::uint32_t>(
                std::min<std::int64_t>(grant->milliseconds, std::numeric_limits<std::uint32_t>::max())));
            return grpc::Status::OK;
        }
        const auto surface = q->has_live() ? "live" : "archive";
        if (!q->privacy_regions().empty() &&
            access::delivery_regions(db, q->point().site_id(), q->point().point_id(), surface) !=
                q->privacy_regions())
            return {grpc::StatusCode::PERMISSION_DENIED, "Privacy policy changed"};
        const auto expiry = access::browser_point_expiry(
            db, q->browser_session_id(), q->point().site_id(), q->point().point_id(),
            q->privacy_regions().empty() ? surface : "", q->has_live() ? "" : std::to_string(bounds.start),
            q->has_live() ? "" : std::to_string(bounds.end));
        if (!expiry)
            return {grpc::StatusCode::PERMISSION_DENIED, "No applicable media session"};
        const auto rows =
            db.exec(R"sql(
          SELECT least(5000,floor(extract(epoch FROM least(expires_at,$8::timestamptz)-clock_timestamp())*1000))::bigint
          FROM bacuri_media_sessions.media_sessions WHERE id=$1 AND browser=$2 AND site_id=$3 AND point_id=$4 AND NOT closed
          AND NOT (coalesce(presentation,'{}') ? 'export_grant') AND coalesce(presentation->>'mask','')=$9 AND ($7::boolean OR coalesce((presentation->>'converted')::boolean,false)=$10::boolean)
          AND CASE WHEN $7::boolean THEN begin_us IS NULL ELSE begin_us<=$5::bigint AND $5::bigint<$6::bigint AND $6::bigint<=end_us END
        )sql",
                    {q->media_session_id(), q->browser_session_id(), q->point().site_id(), q->point().point_id(),
                     std::to_string(bounds.start), std::to_string(bounds.end), q->has_live() ? "true" : "false",
                     *expiry, q->privacy_regions(), q->archive_h264() ? "true" : "false"});
        if (!rows.size()) {
            return {grpc::StatusCode::PERMISSION_DENIED, "No applicable media session"};
        }
        std::int64_t ms = std::stoll(rows.get(0));
        if (q->has_archive()) {
            const auto limit =
                recordings::delivery_lifetime_ms(db, q->point().site_id(), q->point().point_id(), bounds.end);
            if (!limit)
                return {grpc::StatusCode::PERMISSION_DENIED, "No applicable media session"};
            ms = std::min(ms, *limit);
        }
        db.exec("COMMIT");
        if (ms <= 0) {
            return {grpc::StatusCode::FAILED_PRECONDITION, "Media session expired"};
        }
        lease->set_valid_for_ms(static_cast<unsigned>(ms));
        return grpc::Status::OK;
    } catch (const Failure&) {
        return {grpc::StatusCode::INVALID_ARGUMENT, "Invalid archive interval"};
    } catch (const std::exception&) {
        return {grpc::StatusCode::UNAVAILABLE, "Media authorization unavailable"};
    }
}
grpc::Status MediaAuthority::CloseLive(grpc::ServerContext*, const v1::AuthorizeDeliveryRequest* q,
                                       google::protobuf::Empty*)
{
    if (!q->has_live() || !canonical_id(q->media_session_id()) || !canonical_id(q->point().site_id()) ||
        !canonical_id(q->point().point_id())) {
        return {grpc::StatusCode::INVALID_ARGUMENT, "Invalid live scope"};
    }
    try {
        Database db;
        db.exec("UPDATE bacuri_media_sessions.media_sessions SET closed=true WHERE id=$1 AND browser=$2 AND "
                "site_id=$3 AND point_id=$4 "
                "AND begin_us IS NULL AND NOT closed",
                {q->media_session_id(), q->browser_session_id(), q->point().site_id(), q->point().point_id()});
        return grpc::Status::OK;
    } catch (const std::exception&) {
        return {grpc::StatusCode::UNAVAILABLE, "Media authorization unavailable"};
    }
}
} // namespace bacuri::vms::control
