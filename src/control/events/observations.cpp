#include "control/inventory/operations.hpp"
#include "control/events/observations.hpp"
#include "control/events/video_rules.hpp"
#include "control/recordings/policies.hpp"
#include "control/playback_time.hpp"
#include <grpcpp/grpcpp.h>
#include <sys/stat.h>
#include <limits>

namespace bacuri::vms::control
{
namespace
{
using namespace std::chrono_literals;
using RequestData = v1::SubmitObservationRequest;

bool valid(const RequestData& q)
{
    const auto& binding = q.binding();
    // Leave room for the archive context at both ends of the RFC 3339 range.
    return canonical_id(q.observation_id()) && q.schema_version() == 1 &&
           (q.type() == "heartbeat" || (q.type() == "motion" && q.has_binding())) &&
           (!q.has_binding() || (canonical_id(binding.point_id()) && binding.revision() > 0)) &&
           (!q.has_source_time_us() || (q.source_time_us() >= -62135596800000000LL + alert_context_us &&
                                        q.source_time_us() <= 253402300799999999LL - alert_context_us)) &&
           (!q.has_clock() || (q.has_source_time_us() && canonical_id(q.clock().epoch_id()) &&
                               q.clock().uncertainty_us() <= std::numeric_limits<std::int64_t>::max())) &&
           q.attributes_json().size() <= 16384;
}
void publish(Database& db, const RequestData& q)
{
    // Updating the site row serializes publication numbers in transaction commit order.
    const auto pos = db.exec("INSERT INTO bacuri_events.feed_heads AS h VALUES ($1,1) ON CONFLICT(site_id) "
                             "DO UPDATE SET position=h.position+1 RETURNING position",
                             {q.site_id()})
                         .get(0);
    db.exec("INSERT INTO bacuri_events.alerts(site_id,position,source_id,observation_id,point_id) VALUES "
            "($1,$2,$3,$4,$5)",
            {q.site_id(), pos, q.source_id(), q.observation_id(), q.binding().point_id()});
    db.exec("INSERT INTO bacuri_events.alert_outbox VALUES ($1,$2)", {q.site_id(), pos});
    db.exec("DELETE FROM bacuri_events.alert_outbox WHERE site_id=$1 AND position<=$2::bigint-1024",
            {q.site_id(), pos});
}
} // namespace
Observations::Observations() : site_{setting("BACURI_SITE_ID")}, source_{setting("BACURI_OBSERVATION_SOURCE")}
{
    if (!canonical_id(source_)) {
        throw std::runtime_error("Invalid assigned observation source");
    }
    const auto path = setting("BACURI_OBSERVATION_SOCKET");
    grpc::ServerBuilder builder;
    builder.SetMaxReceiveMessageSize(20 * 1024);
    builder.AddListeningPort("unix:" + path, grpc::experimental::LocalServerCredentials(UDS));
    builder.RegisterService(this);
    server_ = builder.BuildAndStart();
    if (!server_ || chmod(path.c_str(), 0660)) {
        throw std::runtime_error("Cannot open observation socket");
    }
}
Observations::~Observations()
{
    server_->Shutdown(std::chrono::system_clock::now() + 2s);
    server_->Wait();
}
grpc::Status Observations::SubmitObservation(grpc::ServerContext*, const RequestData* q,
                                             v1::RegistrationResult* result)
{
    if (q->site_id() != site_ || q->source_id() != source_) {
        return {grpc::StatusCode::PERMISSION_DENIED, "Different source assignment"};
    }
    return store_observation(*q, *result, 0);
}

grpc::Status store_observation(const RequestData& q, v1::RegistrationResult& result, std::int64_t camera_revision,
                               bool detector)
{
    if (!valid(q)) {
        return {grpc::StatusCode::INVALID_ARGUMENT, "Invalid observation"};
    }
    bool supported = true;
    auto accept = [&](int depth, Json::parse_event_t, Json& value) {
        if (depth > 32 || (value.is_string() && value.get_ref<const std::string&>().contains('\0'))) {
            supported = false;
        }
        return supported;
    };
    const auto attributes = Json::parse(q.attributes_json(), accept, false);
    if (!supported || !attributes.is_object()) {
        return {grpc::StatusCode::INVALID_ARGUMENT, "Invalid attributes"};
    }
    try {
        Database db;
        db.exec("BEGIN");
        const auto point = q.binding().point_id();
        const auto revision = q.has_binding() ? std::to_string(q.binding().revision()) : "";
        if (q.has_binding() && !inventory::has_binding(db, q.site_id(), point, revision, detector)) {
            return {grpc::StatusCode::NOT_FOUND, "Unknown binding"};
        }
        if (camera_revision) {
            // Serialize acceptance with replacement and reject detached event readers.
            if (!inventory::lock_camera_revision(db, q.site_id(), point, q.source_id(),
                                                 std::to_string(camera_revision)))
                return {grpc::StatusCode::NOT_FOUND, "Camera configuration changed"};
        }
        const auto source_time = q.has_source_time_us() ? std::to_string(q.source_time_us()) : "";
        const auto epoch = q.clock().epoch_id();
        const auto uncertainty = q.has_clock() ? std::to_string(q.clock().uncertainty_us()) : "";
        const auto args = {q.site_id(),
                           q.source_id(),
                           q.observation_id(),
                           q.type(),
                           point,
                           revision,
                           source_time,
                           epoch,
                           uncertainty,
                           attributes.dump(),
                           camera_revision ? std::string{"onvif"} : std::string{"local_source"}};
        const auto inserted = db.exec(R"sql(
            WITH stamp AS MATERIALIZED (SELECT (extract(epoch FROM clock_timestamp())*1000000)::bigint AS t)
            INSERT INTO bacuri_events.observations(site_id,source_id,observation_id,type,schema_version,point_id,binding_revision,
                origin,observed_at_utc_us,received_at_utc_us,source_time_us,time_basis,clock_epoch_id,uncertainty_us,attributes)
            SELECT $1,$2,$3,$4,1,nullif($5,'')::uuid,nullif($6,'')::bigint,$11,
                CASE WHEN $8='' THEN t ELSE nullif($7,'')::bigint END,t,nullif($7,'')::bigint,
                CASE WHEN $8='' THEN 'reception' ELSE 'source_clock' END,nullif($8,'')::uuid,nullif($9,'')::bigint,$10::jsonb FROM stamp
            ON CONFLICT(site_id,source_id,observation_id) DO NOTHING RETURNING observed_at_utc_us
        )sql",
                                      args);
        if (!inserted.size() && !db.exec(R"sql(
            SELECT 1 FROM bacuri_events.observations WHERE site_id=$1 AND source_id=$2 AND observation_id=$3 AND type=$4
            AND schema_version=1 AND point_id IS NOT DISTINCT FROM nullif($5,'')::uuid
            AND binding_revision IS NOT DISTINCT FROM nullif($6,'')::bigint AND source_time_us IS NOT DISTINCT FROM nullif($7,'')::bigint
            AND origin=$11 AND ($11='onvif' OR (clock_epoch_id IS NOT DISTINCT FROM nullif($8,'')::uuid AND uncertainty_us IS NOT DISTINCT FROM nullif($9,'')::bigint))
            AND (attributes=$10::jsonb OR ($11='onvif' AND NOT attributes ? 'data' AND attributes=($10::jsonb - 'data')))
        )sql",
                                         args)
                                     .size()) {
            return {grpc::StatusCode::ALREADY_EXISTS, "Observation identity has different content"};
        }
        if (inserted.size() && q.type() == "motion") {
            const auto decision = events::evaluate_observation(db, q.site_id(), point, revision, q.source_id(),
                                                               q.observation_id(), std::stoll(inserted.get(0)),
                                                               detector, bool(camera_revision), attributes);
            if (detector && !decision.record && !decision.notify &&
                !db.exec("SELECT FROM bacuri_events.video_matches WHERE site_id=$1 AND source_id=$2 AND "
                         "observation_id=$3",
                         {q.site_id(), q.source_id(), q.observation_id()})
                     .size()) {
                result.set_state(v1::REGISTRATION_STATE_COMMITTED);
                return grpc::Status::OK;
            }
            if (decision.record)
                recordings::motion_recording(db, q.site_id(), point, q.binding().revision(),
                                             std::stoll(inserted.get(0)));
            if (decision.notify)
                publish(db, q);
        }
        db.exec("COMMIT");
        result.set_state(inserted.size() ? v1::REGISTRATION_STATE_COMMITTED : v1::REGISTRATION_STATE_REPLAYED);
        return grpc::Status::OK;
    } catch (const std::exception&) {
        return {grpc::StatusCode::UNAVAILABLE, "Observation journal unavailable"};
    }
}
} // namespace bacuri::vms::control
