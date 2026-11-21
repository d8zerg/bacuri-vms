#include "control/media_authority.hpp"
#include "control/playback_time.hpp"
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
        const auto rows =
            db.exec(R"sql(
          SELECT least(5000,floor(extract(epoch FROM valid_until-clock_timestamp())*1000),
              CASE WHEN NOT $7::boolean THEN floor(($6::numeric-bacuri_archive.retention_now_us()+
                    (SELECT days FROM archive_retention WHERE site_id=$3)*86400000000::bigint)/1000) END)::bigint
          FROM media_rights WHERE id=$1 AND browser=$2 AND site_id=$3 AND point_id=$4 AND NOT closed
          AND CASE WHEN $7::boolean THEN begin_us IS NULL ELSE begin_us<=$5::bigint AND $5::bigint<$6::bigint AND $6::bigint<=end_us AND $6::bigint>bacuri_archive.retention_cutoff($3) END
        )sql",
                    {q->media_session_id(), q->browser_session_id(), q->point().site_id(), q->point().point_id(),
                     std::to_string(bounds.start), std::to_string(bounds.end), q->has_live() ? "true" : "false"});
        if (!rows.size()) {
            return {grpc::StatusCode::PERMISSION_DENIED, "No applicable media session"};
        }
        const auto ms = std::stoll(rows.get(0));
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
        db.exec("UPDATE media_sessions SET closed=true WHERE id=$1 AND browser=$2 AND site_id=$3 AND point_id=$4 "
                "AND begin_us IS NULL AND NOT closed",
                {q->media_session_id(), q->browser_session_id(), q->point().site_id(), q->point().point_id()});
        return grpc::Status::OK;
    } catch (const std::exception&) {
        return {grpc::StatusCode::UNAVAILABLE, "Media authorization unavailable"};
    }
}
} // namespace bacuri::vms::control
