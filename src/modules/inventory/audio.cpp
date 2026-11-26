#include "domain/archive.hpp"
#include "modules/inventory/registry.hpp"
#include "modules/access/operations.hpp"
#include "modules/recordings/operations.hpp"
#include "modules/recordings/policies.hpp"
#include "media/lease.hpp"
#include "platform/rpc_status.hpp"
#include <sodium.h>

namespace bacuri::vms::control
{
void CameraRegistry::audio(const Request& req, Response& res, const std::string& site,
                           const std::string& point) const
{
    auto body = object_body(req, {"mode", "device_id", "session", "sequence", "at_us", "pcm"});
    const auto mode = string_field(body, "mode", 12);
    const bool archive = mode == "archive", talk = mode == "talk" || mode == "mute",
               record = mode == "record" || mode == "record_stop";
    if (!archive && !talk && !record && mode != "listen")
        throw Failure{ErrorCode::invalid_request};
    const auto device = string_field(body, "device_id", 36);
    if (!canonical_id(device))
        throw Failure{ErrorCode::invalid_request};
    if (body.contains("at_us") &&
        (!body["at_us"].is_number_integer() || body["at_us"] < 0 || body["at_us"] > 9007199254740991LL))
        throw Failure{ErrorCode::invalid_request};
    auto at = body.value("at_us", std::int64_t{});
    if (archive && !at)
        throw Failure{ErrorCode::invalid_request};
    postgresql::Database db;
    db.exec("BEGIN");
    access::lock_policy(db, site, true);
    auto user = sessions_.authenticate(db, req, true);
    auto role = access::site_role(db, site, user.user, access::RoleLock::shared);
    const auto begin = archive ? std::to_string(at) : "", end = archive ? std::to_string(at + 1000000) : "";
    const std::string surface = archive ? "archive" : "live";
    const bool stopping = mode == "mute" || mode == "record_stop";
    if (!stopping && !access::allowed(db, site, user.user, point, "view", surface, begin, end))
        throw Failure{ErrorCode::forbidden};
    if (!stopping && !access::allowed(db, site, user.user, point,
                                      talk     ? "audio_talk"
                                      : record ? "audio_record"
                                               : "audio",
                                      surface, begin, end))
        throw Failure{ErrorCode::forbidden};
    if (mode == "record" && recordings::recording_plan(db, site).contains(point))
        throw Failure{ErrorCode::recording_policy_managed};
    v1::AudioRequest q;
    q.set_operator_id(user.user);
    q.set_site_id(site);
    q.set_point_id(point);
    q.set_device_id(device);
    q.set_at_us(at);
    if (archive) {
        q.set_mode(v1::AudioRequest::ARCHIVE);
        recordings::require_available(db, site);
        const auto blocks = recordings::manifests_for_export(db, site, point, begin, end, "");
        for (const auto& m : blocks) {
            if (!m.audio)
                continue;
            const auto& sound = *m.audio;
            auto start = sound.start_us, size = sound.size_bytes;
            auto cursor = std::max(at, start);
            using Pcm = domain::ArchiveSound;
            auto offset =
                (cursor - start + Pcm::sample_period_us - 1) / Pcm::sample_period_us * Pcm::bytes_per_sample;
            auto length = std::min({Pcm::max_chunk_bytes, size - offset,
                                    (at + 1'000'000 - cursor) / Pcm::sample_period_us * Pcm::bytes_per_sample});
            if (length <= 0)
                continue;
            q.set_block_id(m.recording.block().id.to_string());
            q.set_sha256(archive::digest_hex(sound.sha256));
            q.set_at_us(offset);
            q.set_length(static_cast<std::uint32_t>(length));
            at = start + offset / Pcm::bytes_per_sample * Pcm::sample_period_us;
            break;
        }
        if (q.block_id().empty())
            throw Failure{ErrorCode::camera_audio_missing};
    } else {
        auto row = db.exec(
            "SELECT cfg.settings#>>'{onvif_control,audio_input}',cfg.settings#>>'{onvif_control,audio_output}' "
            "FROM bacuri_inventory.camera_registry r JOIN bacuri_inventory.configuration_progress p "
            "USING(site_id,device_id) "
            "JOIN bacuri_inventory.configuration_versions cfg ON "
            "(cfg.site_id,cfg.device_id,cfg.revision)=(r.site_id,r.device_id,p.desired_revision) "
            "WHERE r.site_id=$1 AND r.point_id=$2 AND r.device_id=$3 FOR NO KEY UPDATE OF r",
            {site, point, device});
        if (!row.size())
            throw Failure{ErrorCode::camera_binding_conflict};
        if (row.get(0, talk ? 1 : 0) != "true")
            throw Failure{ErrorCode::camera_audio_unsupported};
        q.set_mode(talk ? v1::AudioRequest::TALK : record ? v1::AudioRequest::RECORD : v1::AudioRequest::LISTEN);
        if (mode == "record_stop")
            q.set_mode(v1::AudioRequest::RECORD_STOP);
        if (talk) {
            const auto session = string_field(body, "session", 36);
            if (!canonical_id(session))
                throw Failure{ErrorCode::invalid_request};
            q.set_talk_session_id(session);
            postgresql::Database journal;
            if (mode == "mute") {
                if (!journal
                         .exec("UPDATE bacuri_inventory.camera_talk_sessions SET "
                               "deadline=clock_timestamp(),expires_at=clock_timestamp() "
                               "WHERE site_id=$1 AND device_id=$2 AND session_id=$3 AND user_id=$4 RETURNING 1",
                               {site, device, session, user.user})
                         .size())
                    throw Failure{ErrorCode::camera_ptz_busy};
                q.set_mode(v1::AudioRequest::MUTE);
            } else {
                const auto encoded = string_field(body, "pcm", 11000);
                std::string pcm(8000, '\0');
                std::size_t bytes{};
                if (sodium_base642bin(reinterpret_cast<unsigned char*>(pcm.data()), pcm.size(), encoded.data(),
                                      encoded.size(), nullptr, &bytes, nullptr,
                                      sodium_base64_VARIANT_ORIGINAL) != 0 ||
                    !bytes || bytes % 320 != 0 || !body.contains("sequence") ||
                    !body["sequence"].is_number_unsigned() || body["sequence"] > 65535)
                    throw Failure{ErrorCode::invalid_request};
                pcm.resize(bytes);
                q.set_pcm(std::move(pcm));
                auto seq = body["sequence"].get<int>();
                if (seq && !journal
                                .exec("SELECT 1 FROM bacuri_inventory.camera_talk_sessions WHERE site_id=$1 AND "
                                      "device_id=$2 AND session_id=$3",
                                      {site, device, session})
                                .size())
                    throw Failure{ErrorCode::camera_ptz_busy};
                auto acquired = journal.exec(R"sql(
                    INSERT INTO bacuri_inventory.camera_talk_sessions AS current VALUES($1,$2,$3,$4,$5,clock_timestamp()+interval '3 seconds',clock_timestamp()+interval '30 seconds')
                    ON CONFLICT(site_id,device_id) DO UPDATE SET user_id=excluded.user_id,session_id=excluded.session_id,
                        last_sequence=excluded.last_sequence,expires_at=least(excluded.expires_at,CASE WHEN excluded.session_id=current.session_id THEN current.deadline ELSE excluded.deadline END),
                        deadline=CASE WHEN excluded.session_id=current.session_id THEN current.deadline ELSE excluded.deadline END
                    WHERE (current.session_id=excluded.session_id AND current.user_id=excluded.user_id AND current.deadline>clock_timestamp()
                           AND current.expires_at>clock_timestamp() AND excluded.last_sequence>current.last_sequence)
                       OR (current.session_id<>excluded.session_id AND excluded.last_sequence=0 AND
                           (current.expires_at<=clock_timestamp() OR $6::boolean))
                    RETURNING greatest(0, floor(extract(epoch FROM expires_at-clock_timestamp())*1000))::integer
                )sql",
                                             {site, device, user.user, session, std::to_string(seq),
                                              role == "administrator" ? "true" : "false"});
                if (!acquired.size() || acquired.get(0) == "0")
                    throw Failure{ErrorCode::camera_ptz_busy};
                q.set_talk_ttl_ms(static_cast<std::uint32_t>(std::stoul(acquired.get(0))));
            }
        }
    }
    auto recorder = v1::RecorderControl::NewStub(media::local_channel("BACURI_SOCKET"));
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds{3});
    v1::AudioChunk chunk;
    auto status = recorder->ExchangeAudio(&context, q, &chunk);
    if (!status.ok()) {
        if (const auto cause = platform::rpc_problem(status))
            throw Failure{*cause};
        throw Failure{status.error_code() == grpc::StatusCode::FAILED_PRECONDITION
                          ? ErrorCode::camera_binding_conflict
                          : ErrorCode::camera_audio_unavailable};
    }
    db.exec("COMMIT");
    Json reply = Json::object();
    if (!chunk.pcm().empty()) {
        std::unique_ptr<gchar, decltype(&g_free)> b64{
            g_base64_encode(reinterpret_cast<const guchar*>(chunk.pcm().data()), chunk.pcm().size()), g_free};
        reply = {{"start_us", archive ? at : chunk.start_us()}, {"pcm", b64.get()}};
    }
    respond(res, reply);
}
} // namespace bacuri::vms::control
