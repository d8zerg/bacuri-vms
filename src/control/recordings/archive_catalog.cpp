#include "control/events/video_rules.hpp"
#include "control/events/observations.hpp"
#include "control/recordings/archive_catalog.hpp"
#include "archive/analysis.hpp"
#include "control/recordings/policies.hpp"
#include "archive_sql.hpp"
#include "control/database/database.hpp"
#include "control/support.hpp"
#include <grpcpp/grpcpp.h>
#include <sys/stat.h>

namespace bacuri::vms::control
{
namespace
{
// Serialize late deliveries with a committed retirement decision or a catalog rebuild.
bool retired(Database& db, const std::string& site, const std::string& point, std::int64_t end)
{
    db.exec("SELECT pg_advisory_xact_lock_shared(hashtextextended($1,1))", {site});
    return db.exec("SELECT WHERE bacuri_archive.point_cutoff($1,$3)>=$2", {site, std::to_string(end), point})
               .size() != 0;
}
} // namespace

ArchiveCatalog::ArchiveCatalog(archive::Owner owner) : owner_{std::move(owner)}
{
    const auto socket = setting("BACURI_CATALOG_SOCKET");
    grpc::ServerBuilder builder;
    builder.SetMaxReceiveMessageSize(2 * 1024 * 1024);
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

grpc::Status ArchiveCatalog::ReportMotion(grpc::ServerContext*, const v1::SubmitObservationRequest* q,
                                          v1::RegistrationResult* out)
{
    if (q->site_id() != owner_.site || q->source_id() != owner_.node || q->type() != "motion" || !q->has_binding())
        return {grpc::StatusCode::PERMISSION_DENIED, "Different detector assignment"};
    return store_observation(*q, *out, 0, true);
}
grpc::Status ArchiveCatalog::NextIndex(grpc::ServerContext*, const v1::RetentionQuery* q, v1::IndexJob* out)
{
    if (q->site_id() != owner_.site || q->recording_node_id() != owner_.node)
        return {grpc::StatusCode::PERMISSION_DENIED, "Different archive assignment"};
    try {
        Database db;
        auto row = db.exec(R"sql(WITH job AS (
            SELECT i.site_id,i.block_id,i.recording_epoch_id,i.owner_generation,b.manifest
            FROM bacuri_archive.motion_indexes i JOIN bacuri_archive.retained_blocks b
                USING(site_id,block_id,recording_epoch_id,owner_generation)
            WHERE i.site_id=$1 AND b.recording_node_id=$2 AND b.unavailable_reason IS NULL
              AND i.samples IS NULL AND i.retry_at<=now()
              AND NOT EXISTS(SELECT FROM bacuri_archive.archive_recovery WHERE site_id=$1)
            ORDER BY i.retry_at,i.block_id LIMIT 1 FOR UPDATE OF i SKIP LOCKED)
            UPDATE bacuri_archive.motion_indexes i SET retry_at=now()+interval '1 minute'
            FROM job j WHERE (i.site_id,i.block_id,i.recording_epoch_id,i.owner_generation)=
                (j.site_id,j.block_id,j.recording_epoch_id,j.owner_generation)
            RETURNING j.manifest)sql",
                           {owner_.site, owner_.node});
        if (row.size())
            out->set_manifest(row.get(0));
        return grpc::Status::OK;
    } catch (const std::exception&) {
        return {grpc::StatusCode::UNAVAILABLE, "Motion catalog unavailable"};
    }
}
grpc::Status ArchiveCatalog::CommitIndex(grpc::ServerContext*, const v1::IndexResult* q, google::protobuf::Empty*)
{
    try {
        auto m = archive::Json::parse(q->manifest());
        archive::validate_manifest(m);
        if (m.at("site_id") != owner_.site || m.at("recording_node_id") != owner_.node)
            return {grpc::StatusCode::PERMISSION_DENIED, "Different archive assignment"};
        auto index = archive::Json::parse(q->analysis());
        archive::validate_analysis(index, m.at("end_utc_us").get<std::int64_t>() -
                                              m.at("begin_utc_us").get<std::int64_t>());
        Database db;
        db.exec(
            "UPDATE bacuri_archive.motion_indexes i SET samples=$6 FROM bacuri_archive.retained_blocks b WHERE "
            "(i.site_id,i.block_id,i.recording_epoch_id,i.owner_generation)=(b.site_id,b.block_id,b.recording_"
            "epoch_id,b.owner_generation) "
            "AND b.site_id=$1 AND b.block_id=$2 AND b.recording_epoch_id=$3 AND b.owner_generation=$4 AND "
            "b.manifest=$5::jsonb AND b.unavailable_reason IS NULL AND i.samples IS NULL",
            {owner_.site, m.at("block_id"), m.at("recording_epoch_id"), m.at("owner_generation").dump(),
             q->manifest(), q->analysis()});
        return grpc::Status::OK;
    } catch (const std::exception&) {
        return {grpc::StatusCode::INVALID_ARGUMENT, "Invalid motion index"};
    }
}

grpc::Status ArchiveCatalog::AdvanceRetention(grpc::ServerContext*, const v1::RetentionQuery* q,
                                              v1::RetentionPolicy* result)
{
    if (q->site_id() != owner_.site || q->recording_node_id() != owner_.node)
        return {grpc::StatusCode::PERMISSION_DENIED, "Different archive assignment"};
    try {
        Database db;
        db.exec("BEGIN");
        db.exec("SELECT pg_advisory_xact_lock(hashtextextended($1,1))", {owner_.site});
        const auto policy =
            db.exec("SELECT days,reserve_percent FROM bacuri_archive.archive_retention WHERE site_id=$1 FOR SHARE",
                    {owner_.site});
        auto protected_ids = db.exec(
            "SELECT DISTINCT b.block_id FROM bacuri_archive.held_blocks b JOIN bacuri_archive.archive_holds h "
            "ON (h.site_id,h.id)=(b.site_id,b.hold_id) WHERE h.site_id=$1 AND "
            "h.expires_us>bacuri_archive.retention_now_us()",
            {owner_.site});
        if (protected_ids.size() && !q->supports_holds())
            return {grpc::StatusCode::FAILED_PRECONDITION, "Archive protection requires recorder support"};
        for (const auto& id : protected_ids.column())
            result->add_protected_blocks(id);
        for (const auto& point : events::detector_points(db, owner_.site))
            result->add_detector_points(point);
        const auto plan = recordings::recording_plan(db, owner_.site);
        result->set_recording_plan_json(plan.dump());
        result->set_days(policy.size() ? std::stoi(policy.get(0)) : 0);
        result->set_reserve_percent(policy.size() ? std::stoi(policy.get(0, 1)) : 20);
        if (db.exec("SELECT FROM bacuri_archive.archive_recovery WHERE site_id=$1", {owner_.site}).size()) {
            // Continue new recordings on a replacement volume while historical blocks are verified.
            result->set_retirement_suspended(true);
            result->set_cutoff_us(std::numeric_limits<std::int64_t>::min());
            db.exec("COMMIT");
            return grpc::Status::OK;
        }
        db.exec("SELECT bacuri_archive.advance_cutoff($1)", {owner_.site});
        auto boundary =
            db.exec("SELECT end_us FROM bacuri_archive.retired_through WHERE site_id=$1", {owner_.site});
        const auto cutoff =
            boundary.size() ? std::stoll(boundary.get(0)) : std::numeric_limits<std::int64_t>::min();
        recordings::advance_point_retention(db, owner_.site, plan);
        auto local = db.exec(
            "SELECT point_id,cutoff_us FROM bacuri_archive.point_retention WHERE site_id=$1 AND cutoff_us>$2",
            {owner_.site, std::to_string(cutoff)});
        for (int i = 0; i < local.size(); ++i)
            (*result->mutable_point_cutoffs())[local.get(i)] = std::stoll(local.get(i, 1));
        db.exec("DELETE FROM bacuri_archive.recording_windows WHERE site_id=$1 AND "
                "end_us<bacuri_archive.retention_now_us()-90000000",
                {owner_.site});
        if (boundary.size() || local.size())
            db.exec("SELECT bacuri_archive.reap_expired($1,$2)", {owner_.site, std::to_string(cutoff)});
        db.exec("COMMIT");
        result->set_cutoff_us(cutoff);
        return grpc::Status::OK;
    } catch (const std::exception&) {
        return {grpc::StatusCode::UNAVAILABLE, "Retention catalog unavailable"};
    }
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
        if (retired(db, owner_.site, m.at("point_id"), m.at("end_utc_us")) &&
            !db.exec("SELECT WHERE bacuri_archive.hold_until($1,$2)>bacuri_archive.retention_now_us()",
                     {owner_.site, m.at("block_id")})
                 .size()) {
            result->set_state(v1::REGISTRATION_STATE_RETIRED);
            db.exec("COMMIT");
            return grpc::Status::OK;
        }
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
        q->interval().begin_us() >= q->interval().end_us() || !domain::parse_gap_reason(q->reason())) {
        return {grpc::StatusCode::INVALID_ARGUMENT, "Invalid gap"};
    }
    try {
        Database db;
        db.exec("BEGIN");
        if (retired(db, owner_.site, q->point().point_id(), q->interval().end_us())) {
            result->set_state(v1::REGISTRATION_STATE_RETIRED);
            db.exec("COMMIT");
            return grpc::Status::OK;
        }
        auto rows = db.exec(
            "INSERT INTO bacuri_archive.gaps VALUES($1,$2,$3,$4,$5,$6) ON CONFLICT DO NOTHING RETURNING 1",
            {owner_.site, q->point().point_id(), std::to_string(q->binding_revision()),
             std::to_string(q->interval().begin_us()), std::to_string(q->interval().end_us()), q->reason()});
        db.exec("COMMIT");
        result->set_state(rows.size() ? v1::REGISTRATION_STATE_COMMITTED : v1::REGISTRATION_STATE_REPLAYED);
        return grpc::Status::OK;
    } catch (const std::exception&) {
        return {grpc::StatusCode::UNAVAILABLE, "Gap catalog unavailable"};
    }
}
} // namespace bacuri::vms::control
