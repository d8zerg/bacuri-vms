#include "application/control_api/http_json.hpp"
#include "recording/policy_json.hpp"
#include "modules/inventory/selection_json.hpp"
#include "modules/inventory/device_json.hpp"
#include "modules/inventory/registered_camera_json.hpp"
#include "modules/inventory/registry.hpp"
#include "application/control_api/playback_time.hpp"
#include "modules/recordings/policies.hpp"
#include "modules/inventory/enrollment.hpp"
#include "modules/inventory/operations.hpp"
#include "modules/recordings/operations.hpp"
#include "modules/recordings/recorder.hpp"
#include "modules/access/operations.hpp"
#include "modules/inventory/device.hpp"
#include "media/lease.hpp"

namespace bacuri::vms::control
{
namespace
{

std::string etag(const Key& key, const Json& camera)
{
    std::string tag{"\""};
    tag += fingerprint(key, "camera-etag", camera.dump());
    tag += '"';
    return tag;
}

Json read_configuration(const Request& request)
{
    auto body = object_body(request, {"name", "main_url", "secondary_url", "point_id", "credentials",
                                      "replace_device_id", "onvif_selection"});
    const auto name = string_field(body, "name", 256);
    if (name.find_first_not_of(" \r\t\n") == name.npos) {
        throw Failure{ErrorCode::invalid_request};
    }
    for (const auto* field : {"main_url", "secondary_url"}) {
        string_field(body, field, 2048);
    }
    if (body.contains("point_id") && !canonical_id(string_field(body, "point_id", 36))) {
        throw Failure{ErrorCode::invalid_request};
    }
    if (body.contains("credentials") && !body.at("credentials").is_null()) {
        const auto& credentials = body.at("credentials");
        if (!credentials.is_object() || credentials.size() != 2) {
            throw Failure{ErrorCode::invalid_request};
        }
        string_field(credentials, "username", 128);
        string_field(credentials, "password", 1024, true);
    }
    if (body.contains("replace_device_id") &&
        (!body.contains("point_id") || !canonical_id(string_field(body, "replace_device_id", 36))))
        throw Failure{ErrorCode::invalid_request};
    if (body.contains("onvif_selection"))
        string_field(body, "onvif_selection", 24576);
    return body;
}

} // namespace

RegisteredCamera CameraRegistry::camera(postgresql::Database& db, const std::string& site,
                                        const std::string& device)
{
    auto rows = db.exec("SELECT body FROM bacuri_inventory.camera_state WHERE site_id=$1 AND device_id=$2",
                        {site, device});
    if (!rows.size()) {
        throw Failure{ErrorCode::not_found};
    }
    return Json::parse(rows.get(0)).get<RegisteredCamera>();
}

std::string CameraRegistry::site_role(postgresql::Database& db, const Identity& identity, const std::string& site)
{
    if (!canonical_id(site)) {
        throw Failure{ErrorCode::not_found};
    }
    return access::site_role(db, site, identity.user, access::RoleLock::shared);
}

void CameraRegistry::onvif(const Request& req, Response& res, const std::string& site, bool inspect) const
{
    {
        postgresql::Database db;
        db.exec("BEGIN");
        const auto identity = sessions_.authenticate(db, req, true);
        if (site_role(db, identity, site) != "administrator")
            throw Failure{ErrorCode::forbidden};
        db.exec("COMMIT");
    }
    if (inspect) {
        const auto body = object_body(req, {"selection", "username", "password"});
        const auto ticket = string_field(body, "selection", 16384);
        const CameraCredentials credentials{string_field(body, "username", 128),
                                            string_field(body, "password", 1024, true)};
        respond(res, device_family_named("onvif").inspect(networks_, key_, site, ticket, credentials));
    } else {
        respond(res, device_family_named("onvif").discover(networks_, key_, site));
    }
}

void CameraRegistry::issues(const Request& req, Response& res, const std::string& site,
                            const std::string& device) const
{
    postgresql::Database db;
    db.exec("BEGIN");
    auto identity = sessions_.authenticate(db, req, false);
    if (site_role(db, identity, site) != "administrator")
        throw Failure{ErrorCode::forbidden};
    camera(db, site, device);
    db.exec("COMMIT");
    v1::CameraIssueQuery query;
    query.set_site_id(site);
    query.set_device_id(device);
    auto recorder = v1::RecorderControl::NewStub(media::local_channel("BACURI_SOCKET"));
    grpc::ClientContext rpc;
    rpc.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds{1});
    v1::CameraIssues result;
    if (!recorder->GetCameraIssues(&rpc, query, &result).ok())
        throw Failure{ErrorCode::media_service_unavailable};
    auto label = [](v1::StreamIssue issue) {
        return issue == v1::STREAM_ISSUE_TLS            ? "certificate_error"
               : issue == v1::STREAM_ISSUE_INCOMPATIBLE ? "incompatible"
                                                        : "unknown";
    };
    respond(res, {{"main", label(result.main())}, {"secondary", label(result.secondary())}});
}

void CameraRegistry::points(const Request& req, Response& res, const std::string& site) const
{
    postgresql::Database db;
    db.exec("BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY");
    const auto identity = sessions_.authenticate(db, req, false, false);
    const bool admin = access::site_role(db, site, identity.user, access::RoleLock::read) == "administrator";
    auto grants = access::granted_point_ids(db, site, identity.user, "record");
    const auto sound_grants = access::granted_point_ids(db, site, identity.user, "audio_record");
    std::vector<std::string> manual;
    const auto plan = recordings::recording_plan(db, site);
    for (const auto& [id, entry] : plan)
        if (entry.manual && (admin || (std::ranges::find(grants, id) != grants.end() &&
                                       (entry.audio != recording::RecordingAudio::enabled ||
                                        std::ranges::find(sound_grants, id) != sound_grants.end()))))
            manual.push_back(id);
    auto rows = db.exec(
        R"sql(
            SELECT coalesce(jsonb_agg(jsonb_build_object('id',p.point_id,'name',p.name,
              'connection','unknown','recording','unknown') ||
              jsonb_build_object('can_snapshot',bacuri_access.can_use(p.site_id,$10,p.point_id,'export',NULL,NULL,'export')
                  AND bacuri_access.can_use(p.site_id,$10,p.point_id,'view',NULL,NULL,''),
                  'can_preview',bacuri_access.can_use(p.site_id,$10,p.point_id,'view',NULL,NULL,'thumbnail')) ||
              CASE WHEN p.point_id=ANY($9::uuid[]) THEN jsonb_build_object('can_record',true) ELSE '{}'::jsonb END ||
              CASE WHEN p.point_id=ANY($3::uuid[])
              THEN jsonb_build_object('can_export',true) ELSE '{}'::jsonb END ||
              CASE WHEN ($4::boolean OR p.point_id=ANY($6::uuid[]) OR p.point_id=ANY($7::uuid[]) OR p.point_id=ANY($8::uuid[]))
                 AND r.device_id IS NOT NULL
              THEN jsonb_build_object('audio',jsonb_build_object('device_id',r.device_id,
                  'listen',coalesce((cfg.settings#>>'{onvif_control,audio_input}')::boolean,false) AND ($4::boolean OR p.point_id=ANY($6::uuid[])),
                  'archive',$4::boolean OR p.point_id=ANY($6::uuid[]),
                  'record',coalesce((cfg.settings#>>'{onvif_control,audio_input}')::boolean,false) AND ($4::boolean OR p.point_id=ANY($7::uuid[])),
                  'talk',coalesce((cfg.settings#>>'{onvif_control,audio_output}')::boolean,false) AND ($4::boolean OR p.point_id=ANY($8::uuid[]))))
              ELSE '{}'::jsonb END ||
              CASE WHEN ($4::boolean OR p.point_id=ANY($5::uuid[])) AND r.sealed_credentials IS NOT NULL AND (jsonb_array_length(cfg.settings->'onvif_control'->'directions')>0 OR cfg.settings->'onvif_control' ?| ARRAY['Imaging','DeviceIO'] OR (cfg.settings#>>'{onvif_control,ptz_profile}')::boolean)
              THEN jsonb_build_object('ptz',jsonb_build_object('device_id',r.device_id,'commands',cfg.settings->'onvif_control'->'directions'))
              ELSE '{}'::jsonb END ORDER BY p.point_id),'[]')
            FROM bacuri_inventory.observation_points p LEFT JOIN bacuri_inventory.camera_registry r USING(site_id,point_id)
            LEFT JOIN bacuri_inventory.configuration_progress cp ON (cp.site_id,cp.device_id)=(r.site_id,r.device_id)
            LEFT JOIN bacuri_inventory.configuration_versions cfg ON (cfg.site_id,cfg.device_id,cfg.revision)=(r.site_id,r.device_id,cp.desired_revision)
            WHERE p.site_id=$1 AND ($4::boolean OR p.point_id=ANY($2::uuid[]))
        )sql",
        {site,
         admin ? "{}"
               : postgresql::array_parameter(access::granted_point_ids(db, site, identity.user, "view", true)),
         postgresql::array_parameter(access::export_point_ids(db, site, identity.user)), admin ? "true" : "false",
         postgresql::array_parameter(access::granted_point_ids(db, site, identity.user, "ptz")),
         postgresql::array_parameter(access::granted_point_ids(db, site, identity.user, "audio")),
         postgresql::array_parameter(access::granted_point_ids(db, site, identity.user, "audio_record")),
         postgresql::array_parameter(access::granted_point_ids(db, site, identity.user, "audio_talk")),
         postgresql::array_parameter(manual), identity.user});
    const auto policy_revision = access::scope_revision(db, site);
    auto points = Json::parse(rows.get(0));
    std::vector<std::string> ids;
    for (const auto& point : points)
        ids.push_back(point.at("id"));
    const auto recorded = recordings::last_recorded(db, site, ids);
    db.exec("COMMIT");
    // The recorder is asked outside the transaction. Without an answer only the archive is reported.
    const auto recorder = recordings::recorder_snapshot(site);
    const auto now = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now());
    for (auto& point : points) {
        const std::string id = point.at("id");
        if (point.contains("audio") && plan.contains(id))
            point["audio"]["record"] = false;
        point["access_revision"] = policy_revision;
        const auto last = recorded.find(id);
        std::optional<std::chrono::microseconds> since;
        if (last != recorded.end()) {
            since = now.time_since_epoch() - std::chrono::microseconds{last->second.received_until_us};
            point["last_recorded_at"] = utc_text(last->second.end_utc_us);
        }
        const auto status = recordings::recording_status(since, recorder, id);
        point["connection"] = recordings::name(status.connection);
        point["recording"] = recordings::name(status.recording);
        if (status.reason)
            point["recording_reason"] = recordings::name(*status.reason);
    }
    respond(res, points);
}

void CameraRegistry::cameras(const Request& req, Response& res, const std::string& site, std::string device) const
{
    const bool create = req.method() == http::verb::post;
    const bool write = create || req.method() == http::verb::put;
    postgresql::Database db;
    db.exec("BEGIN");
    if (write)
        access::lock_policy(db, site, true);
    const auto identity = sessions_.authenticate(db, req, write);
    const bool admin = site_role(db, identity, site) == "administrator";
    if (!device.empty() && !canonical_id(device)) {
        throw Failure{ErrorCode::not_found};
    }
    std::vector<std::string> configurable;
    if (!admin) {
        configurable = access::granted_point_ids(db, site, identity.user, "configure");
        if (create || configurable.empty())
            throw Failure{ErrorCode::forbidden};
        if (!device.empty() && !db.exec("SELECT FROM bacuri_inventory.camera_registry WHERE site_id=$1 AND "
                                        "device_id=$2 AND point_id=ANY($3::uuid[])",
                                        {site, device, postgresql::array_parameter(configurable)})
                                    .size())
            throw Failure{ErrorCode::forbidden};
    }

    if (!write) {
        if (device.empty()) {
            auto rows = db.exec(
                "SELECT coalesce(jsonb_agg(body ORDER BY device_id),'[]') FROM "
                "bacuri_inventory.camera_state WHERE site_id=$1 AND ($2::boolean OR point_id=ANY($3::uuid[]))",
                {site, admin ? "true" : "false", postgresql::array_parameter(configurable)});
            respond(res, Json::parse(rows.get(0)));
        } else {
            auto current = camera(db, site, device);
            res.set("ETag", etag(key_, current));
            respond(res, current);
        }
        db.exec("COMMIT");
        return;
    }
    auto body = read_configuration(req);
    db.exec("SELECT site_id FROM bacuri_inventory.sites WHERE site_id=$1 FOR UPDATE", {site});
    db.exec("SELECT bacuri_inventory.record_camera_transport_policy($1,$2::jsonb)",
            {site, Json(networks_.legacy_addresses()).dump()});
    const auto request_key = std::string{req["Idempotency-Key"]};
    std::string digest;
    std::string revision = "1";
    Json result;
    Json onvif = Json::object();
    CameraSelection selected;
    if (create) {
        if (!canonical_id(request_key)) {
            throw Failure{ErrorCode::invalid_request};
        }
        digest = fingerprint(key_, "registration", body.dump());
        db.exec("DELETE FROM bacuri_inventory.registration_requests WHERE site_id=$1 AND expires_at<=now()",
                {site});
        auto replay =
            db.exec("SELECT fingerprint,response FROM bacuri_inventory.registration_requests WHERE site_id=$1 AND "
                    "user_id=$2 AND request_key=$3",
                    {site, identity.user, request_key});
        if (replay.size()) {
            if (replay.get(0) != digest) {
                throw Failure{ErrorCode::idempotency_conflict};
            }
            result = Json::parse(replay.get(0, 1));
            device = result.at("id").get<std::string>();
        } else {
            if (body.contains("onvif_selection")) {
                selected = device_family_named("onvif").read_selection(key_, site, body.at("onvif_selection"));
                auto channel = inventory::channel_configuration(key_, selected, body.at("main_url"),
                                                                body.at("secondary_url"));
                if (inventory::registered_channel(db, site, channel))
                    throw Failure{ErrorCode::camera_already_registered};
            }
            device = inventory::register_device(db, site, body.get<CameraRegistration>());
        }
    } else {
        if (body.contains("replace_device_id"))
            throw Failure{ErrorCode::invalid_request};
        auto current = camera(db, site, device);
        if (current.identification) {
            if (body["main_url"] != current.main_url || body["secondary_url"] != current.secondary_url) {
                if (!body.contains("onvif_selection"))
                    throw Failure{ErrorCode::camera_binding_conflict};
            }
            onvif = *current.identification;
        }
        if (!body.contains("onvif_selection")) {
            auto saved = db.exec("SELECT settings->'onvif_control' FROM bacuri_inventory.configuration_versions "
                                 "WHERE site_id=$1 "
                                 "AND device_id=$2 AND revision=$3",
                                 {site, device, current.desired_revision})
                             .get(0);
            if (!saved.empty())
                body["onvif_control"] = Json::parse(saved);
        }
        auto tag = req[http::field::if_match];
        if (tag.empty()) {
            throw Failure{ErrorCode::precondition_required};
        }
        if (tag != etag(key_, current)) {
            res.set("ETag", etag(key_, current));
            throw HttpProblem{{ErrorCode::precondition_failed}, {{"current", current}}};
        }
        if (body.contains("point_id") && body["point_id"] != current.point_id) {
            throw Failure{ErrorCode::invalid_request};
        }
        revision = db.exec("SELECT desired_revision+1 FROM bacuri_inventory.configuration_progress WHERE "
                           "site_id=$1 AND device_id=$2",
                           {site, device})
                       .get(0);
        db.exec("UPDATE bacuri_inventory.devices SET name=$3 WHERE site_id=$1 AND device_id=$2",
                {site, device, body.at("name").get<std::string>()});
    }
    if (result.is_null()) {
        if (!networks_.permits(body.at("main_url").get_ref<const std::string&>()) ||
            !networks_.permits(body.at("secondary_url").get_ref<const std::string&>())) {
            throw Failure{ErrorCode::camera_transport_denied};
        }
        if (body.contains("onvif_selection")) {
            if (!create) {
                selected = device_family_named("onvif").read_selection(key_, site, body.at("onvif_selection"));
                const auto id = selected.identity.endpoint_id;
                if (!onvif.empty() && onvif["endpoint_id"] != id)
                    throw Failure{ErrorCode::camera_binding_conflict};
            }
            auto config =
                inventory::channel_configuration(key_, selected, body.at("main_url"), body.at("secondary_url"));
            if (!create) {
                auto match = inventory::registered_channel(db, site, config);
                if (match && match->id != device)
                    throw Failure{ErrorCode::camera_already_registered};
                if (!onvif.empty() && !match)
                    throw Failure{ErrorCode::camera_binding_conflict};
            }
            onvif = config.identity;
            body["onvif_control"] = config.control;
        }
        if (!onvif.empty())
            body["onvif"] = std::move(onvif);
        inventory::write_version(db, key_, site, device, revision, body.get<CameraVersion>());
        result = camera(db, site, device);
        db.exec(R"sql(
                INSERT INTO bacuri_inventory.configuration_audit(request_id,user_id,site_id,device_id,action,previous_revision,revision,outcome)
                VALUES ($6,$1,$2,$3,$4,nullif($5::bigint-1,0),$5,'saved')
            )sql",
                {identity.user, site, device,
                 body.contains("replace_device_id") ? "replace"
                 : create                           ? "register"
                                                    : "update",
                 revision, std::string{res["X-Request-Id"]}});
        if (create) {
            db.exec("INSERT INTO bacuri_inventory.registration_requests VALUES ($1,$2,$3,$4,$5,now()+interval '24 "
                    "hours')",
                    {site, identity.user, request_key, digest, result.dump()});
        }
    }
    db.exec("COMMIT");
    if (create) {
        res.set("Location", "/api/v1/sites/" + site + "/cameras/" + device);
    }
    res.set("ETag", etag(key_, result));
    respond(res, result, create ? 201 : 200);
}
} // namespace bacuri::vms::control
