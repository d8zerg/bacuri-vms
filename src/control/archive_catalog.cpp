#include "control/archive_catalog.hpp"
#include "archive_sql.hpp"
#include "control/database.hpp"
#include "control/support.hpp"
#include <grpcpp/grpcpp.h>
#include <sys/stat.h>

namespace bacuri::vms::control
{
ArchiveCatalog::ArchiveCatalog(archive::Owner owner) : owner_{std::move(owner)}
{
    const auto socket = setting("BACURI_CATALOG_SOCKET");
    grpc::ServerBuilder builder;
    builder.SetMaxReceiveMessageSize(archive::manifest_limit + 1024);
    builder.AddListeningPort("unix:" + socket, grpc::experimental::LocalServerCredentials(UDS));
    builder.RegisterService(this);
    server_ = builder.BuildAndStart();
    if (!server_ || chmod(socket.c_str(), 0660) != 0) {
        throw std::runtime_error("Cannot open local archive catalog");
    }
}
ArchiveCatalog::~ArchiveCatalog()
{
    server_->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds{2});
    server_->Wait();
}

grpc::Status ArchiveCatalog::RegisterBlock(grpc::ServerContext*, const v1::RegisterBlockRequest* request,
                                           v1::RegistrationResult* result)
{
    archive::Json m;
    try {
        if (request->verification_generation() < 0 || request->manifest_json().size() > archive::manifest_limit ||
            !v1::BlockAvailability_IsValid(request->availability())) {
            throw std::invalid_argument("Invalid archive request");
        }
        m = archive::Json::parse(request->manifest_json());
        archive::validate_manifest(m);
    } catch (const std::exception&) {
        return {grpc::StatusCode::INVALID_ARGUMENT, "Invalid archive manifest"};
    }
    if (m.at("site_id") != owner_.site || m.at("recording_node_id") != owner_.node) {
        return {grpc::StatusCode::PERMISSION_DENIED, "Different archive assignment"};
    }
    try {
        Database db;
        db.exec("BEGIN");
        const bool inserted = db.exec(archive::sql::insert_block, {m.dump()}).size() != 0;
        const std::string id = m.at("block_id"), epoch = m.at("recording_epoch_id");
        const auto generation = std::to_string(m.at("owner_generation").get<std::int64_t>());
        if (!inserted) {
            auto prior = db.exec(archive::sql::read_block, {owner_.site, id, epoch, generation});
            if (archive::Json::parse(prior.get(0)) != m) {
                return {grpc::StatusCode::ALREADY_EXISTS, "Block identity has different content"};
            }
        }
        const auto reason = request->availability() == v1::BLOCK_AVAILABILITY_MISSING   ? "missing"
                            : request->availability() == v1::BLOCK_AVAILABILITY_CORRUPT ? "corrupt"
                                                                                        : "";
        db.exec("UPDATE bacuri_archive.blocks SET unavailable_reason=nullif($5,''),verification_generation=$6 "
                "WHERE site_id=$1 AND block_id=$2 AND recording_epoch_id=$3 AND owner_generation=$4 "
                "AND verification_generation<=$6 AND (verification_generation<>$6 OR unavailable_reason IS "
                "DISTINCT FROM nullif($5,''))",
                {owner_.site, id, epoch, generation, reason,
                 request->verification_generation() ? std::to_string(request->verification_generation())
                                                    : generation});
        db.exec("COMMIT");
        result->set_state(inserted ? v1::REGISTRATION_STATE_COMMITTED : v1::REGISTRATION_STATE_REPLAYED);
        return grpc::Status::OK;
    } catch (const std::exception&) {
        return {grpc::StatusCode::UNAVAILABLE, "Archive catalog unavailable"};
    }
}
grpc::Status ArchiveCatalog::RegisterGap(grpc::ServerContext*, const v1::RegisterGapRequest* q,
                                         v1::RegistrationResult* result)
{
    if (q->point().site_id() != owner_.site || q->recording_node_id() != owner_.node) {
        return {grpc::StatusCode::PERMISSION_DENIED, "Different archive assignment"};
    }
    if (!canonical_id(q->point().point_id()) || q->binding_revision() < 1 ||
        q->interval().begin_us() >= q->interval().end_us() ||
        (q->reason() != "source_offline" && q->reason() != "overload" && q->reason() != "storage_failure")) {
        return {grpc::StatusCode::INVALID_ARGUMENT, "Invalid gap"};
    }
    try {
        Database db;
        auto rows = db.exec(
            "INSERT INTO bacuri_archive.gaps VALUES($1,$2,$3,$4,$5,$6) ON CONFLICT DO NOTHING RETURNING 1",
            {owner_.site, q->point().point_id(), std::to_string(q->binding_revision()),
             std::to_string(q->interval().begin_us()), std::to_string(q->interval().end_us()), q->reason()});
        result->set_state(rows.size() ? v1::REGISTRATION_STATE_COMMITTED : v1::REGISTRATION_STATE_REPLAYED);
        return grpc::Status::OK;
    } catch (const std::exception&) {
        return {grpc::StatusCode::UNAVAILABLE, "Gap catalog unavailable"};
    }
}
} // namespace bacuri::vms::control
