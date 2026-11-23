#include "control/inventory/telemetry.hpp"
#include "control/inventory/registry.hpp"
#include "control/inventory/enrollment.hpp"
#include "control/inventory/operations.hpp"
#include "control/recordings/operations.hpp"
#include "control/access/operations.hpp"
#include "control/api.hpp"
#include "control/onvif.hpp"
#include "control/playback_time.hpp"
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

Json camera(Database& db, const std::string& site, const std::string& device)
{
    auto rows = db.exec("SELECT body FROM bacuri_inventory.camera_state WHERE site_id=$1 AND device_id=$2",
                        {site, device});
    if (!rows.size()) {
        throw Failure{404, "not_found"};
    }
    return Json::parse(rows.get(0));
}

std::string site_role(Database& db, const Identity& identity, const std::string& site)
{
    if (!canonical_id(site)) {
        throw Failure{404, "not_found"};
    }
    return access::site_role(db, site, identity.user, access::RoleLock::shared);
}

Json read_configuration(const Request& request)
{
    auto body = object_body(request, {"name", "main_url", "secondary_url", "point_id", "credentials",
                                      "replace_device_id", "onvif_selection"});
    const auto name = string_field(body, "name", 256);
    if (name.find_first_not_of(" \r\t\n") == name.npos) {
        throw Failure{400, "invalid_request"};
    }
    for (const auto* field : {"main_url", "secondary_url"}) {
        string_field(body, field, 2048);
    }
    if (body.contains("point_id") && !canonical_id(string_field(body, "point_id", 36))) {
        throw Failure{400, "invalid_request"};
    }
    if (body.contains("credentials") && !body.at("credentials").is_null()) {
        const auto& credentials = body.at("credentials");
        if (!credentials.is_object() || credentials.size() != 2) {
            throw Failure{400, "invalid_request"};
        }
        string_field(credentials, "username", 128);
        string_field(credentials, "password", 1024, true);
    }
    if (body.contains("replace_device_id") &&
        (!body.contains("point_id") || !canonical_id(string_field(body, "replace_device_id", 36))))
        throw Failure{400, "invalid_request"};
    if (body.contains("onvif_selection"))
        string_field(body, "onvif_selection", 24576);
    return body;
}

} // namespace
namespace inventory
{
void write_version(Database& db, const Key& key, const std::string& site, const std::string& device,
                   const std::string& revision, const Json& body)
{
    Json settings{{"main_url", body.at("main_url")}, {"secondary_url", body.at("secondary_url")}};
    if (body.contains("onvif"))
        settings["onvif"] = body.at("onvif");
    if (body.contains("onvif_control"))
        settings["onvif_control"] = body.at("onvif_control");
    db.exec("INSERT INTO bacuri_inventory.configuration_versions VALUES ($1,$2,$3,$4)",
            {site, device, revision, settings.dump()});
    db.exec(R"sql(
        INSERT INTO bacuri_inventory.configuration_progress(site_id,device_id,desired_revision,application_state)
        VALUES ($1,$2,$3,'pending') ON CONFLICT (site_id,device_id) DO UPDATE
        SET desired_revision=excluded.desired_revision,application_state='pending',failure_code=NULL
    )sql",
            {site, device, revision});
    if (body.contains("credentials")) {
        const auto& credentials = body.at("credentials");
        if (credentials.is_null()) {
            db.exec("UPDATE bacuri_inventory.camera_registry SET secret_revision=NULL,sealed_credentials=NULL "
                    "WHERE site_id=$1 AND "
                    "device_id=$2",
                    {site, device});
        } else {
            const auto context = site + "/" + device + "/" + revision;
            db.exec("UPDATE bacuri_inventory.camera_registry SET secret_revision=$3,sealed_credentials=$4 WHERE "
                    "site_id=$1 AND "
                    "device_id=$2",
                    {site, device, revision, seal(key, context, credentials.dump())});
        }
    }
    db.exec(R"sql(
        INSERT INTO bacuri_inventory.recorder_delivery(site_id,desired_revision) VALUES ($1,1)
        ON CONFLICT (site_id) DO UPDATE SET desired_revision=recorder_delivery.desired_revision+1
    )sql",
            {site});
}

std::string register_device(Database& db, const std::string& site, const Json& body)
{
    const auto name = body.at("name").get<std::string>();
    std::string point;
    if (body.contains("point_id")) {
        point = body.at("point_id").get<std::string>();
        if (!db.exec("SELECT 1 FROM bacuri_inventory.observation_points WHERE site_id=$1 AND point_id=$2",
                     {site, point})
                 .size()) {
            throw Failure{404, "not_found"};
        }
        if (body.contains("replace_device_id")) {
            if (!db.exec("DELETE FROM bacuri_inventory.camera_registry WHERE site_id=$1 AND point_id=$2 AND "
                         "device_id=$3 RETURNING "
                         "device_id",
                         {site, point, body.at("replace_device_id").get<std::string>()})
                     .size())
                throw Failure{409, "camera_binding_conflict"};
        }
        if (db.exec("SELECT 1 FROM bacuri_inventory.camera_registry WHERE site_id=$1 AND point_id=$2",
                    {site, point})
                .size()) {
            throw Failure{400, "invalid_request"};
        }
    } else {
        point = db.exec("INSERT INTO bacuri_inventory.observation_points VALUES ($1,gen_random_uuid(),$2) "
                        "RETURNING point_id",
                        {site, name})
                    .get(0);
    }
    auto device =
        db.exec("INSERT INTO bacuri_inventory.devices VALUES ($1,gen_random_uuid(),$2) RETURNING device_id",
                {site, name})
            .get(0);
    auto channel =
        db.exec("INSERT INTO bacuri_inventory.channels VALUES ($1,gen_random_uuid(),$2,0) RETURNING channel_id",
                {site, device})
            .get(0);
    db.exec("INSERT INTO bacuri_inventory.stream_profiles SELECT $1,gen_random_uuid(),$2,role FROM "
            "unnest(ARRAY['main','secondary']) role",
            {site, channel});
    db.exec(R"sql(
        INSERT INTO bacuri_inventory.point_bindings SELECT $1,$2,coalesce(max(binding_revision),0)+1,$3,
        greatest((extract(epoch FROM clock_timestamp())*1000000)::bigint,max(valid_from_utc_us)+1)
        FROM bacuri_inventory.point_bindings WHERE site_id=$1 AND point_id=$2
    )sql",
            {site, point, channel});
    db.exec("INSERT INTO bacuri_inventory.camera_registry(site_id,device_id,point_id) VALUES ($1,$2,$3)",
            {site, device, point});
    return device;
}
Json channel_configuration(const Key& key, const Json& selection, const std::string& main,
                           const std::string& secondary)
{
    const auto& profiles = selection.at("profiles");
    const auto first = profiles.find(fingerprint(key, "onvif-stream", main));
    const auto second = profiles.find(fingerprint(key, "onvif-stream", secondary));
    if (first == profiles.end() || second == profiles.end())
        throw Failure{400, "invalid_request"};
    if (main == secondary || first->value("channel", std::string{}) != second->value("channel", std::string{}))
        throw Failure{422, "camera_incompatible_profiles"};
    auto control = selection.at("control");
    control.update(*first);
    control["secondary_profile"] = second->at("profile");
    Json meta;
    for (const auto* k : {"endpoint_id", "manufacturer", "model", "firmware", "serial", "ptz", "events"})
        meta[k] = selection.at(k);
    if (control.contains("ptz_profile"))
        meta["ptz"] = meta.at("ptz") == true && control.at("ptz_profile") == true;
    meta["channel_token"] = first->value("channel", std::string{});
    Json profile_channels = Json::object(), stream_channels = Json::object();
    for (const auto& p : profiles) {
        if (p.contains("channel") && p.contains("stream_url")) {
            profile_channels[p.at("profile").get_ref<const std::string&>()] = p.at("channel");
            stream_channels[p.at("stream_url").get_ref<const std::string&>()] = p.at("channel");
        }
    }
    return {{"onvif", meta},
            {"onvif_control", control},
            {"main_url", main},
            {"secondary_url", secondary},
            {"profile_channels", std::move(profile_channels)},
            {"stream_channels", std::move(stream_channels)}};
}
Json registered_channel(Database& db, const std::string& site, const Json& config)
{
    const auto& onvif = config.at("onvif");
    const auto channel = onvif.at("channel_token").get<std::string>();
    // Older configurations identify the recorded profile, before a physical source token was stored.
    auto rows = db.exec(R"sql(
        SELECT c.body,coalesce(nullif(c.body->>'channel_token',''),$3::jsonb->>(v.settings#>>'{onvif_control,profile}'),
                               $4::jsonb->>(c.body->>'main_url'),$4::jsonb->>(c.body->>'secondary_url'),'')
        FROM bacuri_inventory.camera_state c JOIN bacuri_inventory.configuration_versions v
          ON (v.site_id,v.device_id,v.revision)=(c.site_id,c.device_id,(c.body->>'desired_revision')::bigint)
        WHERE c.site_id=$1 AND c.body->>'endpoint_id'=$2
    )sql",
                        {site, onvif.at("endpoint_id"), config.at("profile_channels").dump(),
                         config.at("stream_channels").dump()});
    Json match = nullptr;
    for (int i = 0; i < rows.size(); ++i) {
        const auto source = rows.get(i, 1);
        if (!channel.empty() && source.empty())
            throw Failure{409, "camera_binding_conflict"};
        if (channel.empty() || channel == source) {
            if (!match.is_null())
                throw Failure{409, "camera_binding_conflict"};
            match = Json::parse(rows.get(i));
        }
    }
    return match;
}
} // namespace inventory

void CameraRegistry::onvif(const Request& req, Response& res, const std::string& site, bool inspect) const
{
    {
        Database db;
        db.exec("BEGIN");
        const auto identity = sessions_.authenticate(db, req, true);
        if (site_role(db, identity, site) != "administrator")
            throw Failure{403, "forbidden"};
        db.exec("COMMIT");
    }
    if (inspect) {
        const auto body = object_body(req, {"selection", "username", "password"});
        respond(res, inspect_onvif(networks_, key_, site, body));
    } else {
        respond(res, discover_onvif(networks_, key_, site));
    }
}

void CameraRegistry::issues(const Request& req, Response& res, const std::string& site,
                            const std::string& device) const
{
    Database db;
    db.exec("BEGIN");
    auto identity = sessions_.authenticate(db, req, false);
    if (site_role(db, identity, site) != "administrator")
        throw Failure{403, "forbidden"};
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
        throw Failure{503, "media_unavailable"};
    auto label = [](v1::StreamIssue issue) {
        return issue == v1::STREAM_ISSUE_TLS            ? "certificate_error"
               : issue == v1::STREAM_ISSUE_INCOMPATIBLE ? "incompatible"
                                                        : "unknown";
    };
    respond(res, {{"main", label(result.main())}, {"secondary", label(result.secondary())}});
}

void CameraRegistry::previous_recording(const Request& req, Response& res, const std::string& site,
                                        const std::string& device) const
{
    Database db;
    db.exec("BEGIN");
    auto identity = sessions_.authenticate(db, req, false);
    if (site_role(db, identity, site) != "administrator")
        throw Failure{403, "forbidden"};
    const auto current = camera(db, site, device);
    const auto recording =
        recordings::previous_binding_recording(db, site, current.at("point_id"), current.at("binding_revision"));
    if (!recording)
        throw Failure{404, "not_found"};
    db.exec("COMMIT");
    respond(res, {{"binding_revision", recording->revision},
                  {"interval", Window{recording->start, recording->end}.json()}});
}

void CameraRegistry::ptz(const Request& req, Response& res, const std::string& site,
                         const std::string& point) const
{
    Database db;
    db.exec("BEGIN");
    access::lock_policy(db, site, true);
    auto user = sessions_.authenticate(db, req, true);
    auto role = site_role(db, user, site);
    const bool visible = role == "administrator" ? inventory::lock_point(db, site, point)
                                                 : access::lock_point_grant(db, site, point, user.user);
    if (!visible)
        throw Failure{404, "not_found"};
    if (!access::allowed(db, site, user.user, point, "ptz"))
        throw Failure{403, "forbidden"};
    auto body = object_body(req, {"device_id", "command"});
    auto device = string_field(body, "device_id", 36);
    auto command = string_field(body, "command", 16);
    const std::string request_key{req["Idempotency-Key"]};
    if (!canonical_id(device) || !canonical_id(request_key))
        throw Failure{400, "invalid_request"};
    auto rows = db.exec(R"sql(
        SELECT v.settings->'onvif_control', r.secret_revision, r.sealed_credentials
        FROM bacuri_inventory.camera_registry r JOIN bacuri_inventory.configuration_progress p USING(site_id,device_id)
        JOIN bacuri_inventory.configuration_versions v ON (v.site_id,v.device_id,v.revision)=(r.site_id,r.device_id,p.desired_revision)
        WHERE r.site_id=$1 AND r.point_id=$2 AND r.device_id=$3 FOR UPDATE OF r
    )sql",
                        {site, point, device});
    if (!rows.size())
        throw Failure{409, "camera_binding_conflict"};
    // Keep the binding locked while a separate transaction records dispatch before touching the device.
    Database journal;
    journal.exec("DELETE FROM bacuri_inventory.ptz_commands WHERE started_at<now()-interval '24 hours'");
    auto replay = [&] {
        auto saved = journal.exec(
            "SELECT device_id,point_id,command,status,failure_code FROM bacuri_inventory.ptz_commands "
            "WHERE site_id=$1 AND user_id=$2 AND request_key=$3",
            {site, user.user, request_key});
        if (!saved.size())
            return false;
        if (saved.get(0) != device || saved.get(0, 1) != point || saved.get(0, 2) != command)
            throw Failure{409, "idempotency_conflict"};
        if (saved.get(0, 3).empty())
            throw Failure{502, "camera_ptz_unknown"};
        auto status = std::stoi(saved.get(0, 3));
        if (status != 204)
            throw Failure{status, saved.get(0, 4)};
        return true;
    };
    if (!replay()) {
        if (rows.get(0).empty() || rows.get(0, 2).empty())
            throw Failure{422, "camera_ptz_unsupported"};
        auto control = Json::parse(rows.get(0));
        const auto& allowed = control.at("directions");
        if (std::ranges::find(allowed, Json(command)) == allowed.end())
            throw Failure{422, "camera_ptz_unsupported"};
        auto credentials = Json::parse(unseal(rows.get(0, 2), key_, site + "/" + device + "/" + rows.get(0, 1)));
        auto owner = journal.exec("SELECT user_id,lease_until>clock_timestamp() FROM "
                                  "bacuri_inventory.ptz_commands WHERE site_id=$1 AND device_id=$2 "
                                  "ORDER BY started_at DESC LIMIT 1",
                                  {site, device});
        if (owner.size() && owner.get(0, 1) == "t" && owner.get(0) != user.user && role != "administrator")
            throw Failure{409, "camera_ptz_busy"};
        auto created = journal.exec(
            "INSERT INTO bacuri_inventory.ptz_commands(site_id,user_id,request_key,device_id,point_id,command) "
            "VALUES "
            "($1,$2,$3,$4,$5,$6) ON CONFLICT DO NOTHING RETURNING 1",
            {site, user.user, request_key, device, point, command});
        if (created.size()) {
            inventory::refresh_position(journal, site, device);
            try {
                move_onvif(networks_, control, credentials, command);
            } catch (const Failure& e) {
                // A missing SOAP acknowledgement leaves the outcome unknown even when the request reached the
                // device.
                const auto failure = (e.code == "camera_unavailable" || e.code == "camera_protocol_error")
                                         ? Failure{e.status, "camera_ptz_unknown"}
                                         : e;
                journal.exec("UPDATE bacuri_inventory.ptz_commands SET status=$4,failure_code=$5 WHERE site_id=$1 "
                             "AND user_id=$2 "
                             "AND request_key=$3",
                             {site, user.user, request_key, std::to_string(failure.status), failure.code});
                throw failure;
            }
            journal.exec(
                "UPDATE bacuri_inventory.ptz_commands SET status=204,lease_until=clock_timestamp()+CASE WHEN "
                "command='stop' THEN interval '0 seconds' "
                "ELSE interval '1 second' END WHERE site_id=$1 AND user_id=$2 AND request_key=$3",
                {site, user.user, request_key});
        } else if (!replay())
            throw Failure{503, "dependency_unavailable"};
    }
    db.exec("COMMIT");
    res.result(http::status::no_content);
}

void CameraRegistry::points(const Request& req, Response& res, const std::string& site) const
{
    Database db;
    db.exec("BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY");
    const auto identity = sessions_.authenticate(db, req, false, false);
    const bool admin = access::site_role(db, site, identity.user, access::RoleLock::read) == "administrator";
    auto rows = db.exec(
        R"sql(
            SELECT coalesce(jsonb_agg(jsonb_build_object('id',p.point_id,'name',p.name,
              'connection','unknown','recording','unknown') ||
              CASE WHEN p.point_id=ANY($3::uuid[])
              THEN jsonb_build_object('can_export',true) ELSE '{}'::jsonb END ||
              CASE WHEN ($4::boolean OR p.point_id=ANY($5::uuid[])) AND r.sealed_credentials IS NOT NULL AND jsonb_array_length(cfg.settings->'onvif_control'->'directions')>0
              THEN jsonb_build_object('ptz',jsonb_build_object('device_id',r.device_id,'commands',cfg.settings->'onvif_control'->'directions'))
              ELSE '{}'::jsonb END ORDER BY p.point_id),'[]')
            FROM bacuri_inventory.observation_points p LEFT JOIN bacuri_inventory.camera_registry r USING(site_id,point_id)
            LEFT JOIN bacuri_inventory.configuration_progress cp ON (cp.site_id,cp.device_id)=(r.site_id,r.device_id)
            LEFT JOIN bacuri_inventory.configuration_versions cfg ON (cfg.site_id,cfg.device_id,cfg.revision)=(r.site_id,r.device_id,cp.desired_revision)
            WHERE p.site_id=$1 AND ($4::boolean OR p.point_id=ANY($2::uuid[]))
        )sql",
        {site, admin ? "{}" : array_parameter(access::granted_point_ids(db, site, identity.user, "view", true)),
         array_parameter(access::export_point_ids(db, site, identity.user)), admin ? "true" : "false",
         array_parameter(access::granted_point_ids(db, site, identity.user, "ptz"))});
    db.exec("COMMIT");
    respond(res, Json::parse(rows.get(0)));
}

void CameraRegistry::cameras(const Request& req, Response& res, const std::string& site, std::string device) const
{
    const bool create = req.method() == http::verb::post;
    const bool write = create || req.method() == http::verb::put;
    Database db;
    db.exec("BEGIN");
    if (write)
        access::lock_policy(db, site, true);
    const auto identity = sessions_.authenticate(db, req, write);
    const bool admin = site_role(db, identity, site) == "administrator";
    if (!device.empty() && !canonical_id(device)) {
        throw Failure{404, "not_found"};
    }
    std::vector<std::string> configurable;
    if (!admin) {
        configurable = access::granted_point_ids(db, site, identity.user, "configure");
        if (create || configurable.empty())
            throw Failure{403, "forbidden"};
        if (!device.empty() && !db.exec("SELECT FROM bacuri_inventory.camera_registry WHERE site_id=$1 AND "
                                        "device_id=$2 AND point_id=ANY($3::uuid[])",
                                        {site, device, array_parameter(configurable)})
                                    .size())
            throw Failure{403, "forbidden"};
    }

    if (!write) {
        if (device.empty()) {
            auto rows = db.exec(
                "SELECT coalesce(jsonb_agg(body ORDER BY device_id),'[]') FROM "
                "bacuri_inventory.camera_state WHERE site_id=$1 AND ($2::boolean OR point_id=ANY($3::uuid[]))",
                {site, admin ? "true" : "false", array_parameter(configurable)});
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
    Json selected;
    if (create) {
        if (!canonical_id(request_key)) {
            throw Failure{400, "invalid_request"};
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
                throw Failure{409, "idempotency_conflict"};
            }
            result = Json::parse(replay.get(0, 1));
            device = result.at("id").get<std::string>();
        } else {
            if (body.contains("onvif_selection")) {
                selected = read_camera_ticket(key_, site, body.at("onvif_selection"));
                auto channel = inventory::channel_configuration(key_, selected, body.at("main_url"),
                                                                body.at("secondary_url"));
                if (!inventory::registered_channel(db, site, channel).is_null())
                    throw Failure{409, "camera_already_registered"};
            }
            device = inventory::register_device(db, site, body);
        }
    } else {
        if (body.contains("replace_device_id"))
            throw Failure{400, "invalid_request"};
        auto current = camera(db, site, device);
        if (current.contains("endpoint_id")) {
            if (body["main_url"] != current["main_url"] || body["secondary_url"] != current["secondary_url"]) {
                if (!body.contains("onvif_selection"))
                    throw Failure{409, "camera_binding_conflict"};
            }
            for (const auto* field :
                 {"endpoint_id", "model", "manufacturer", "firmware", "serial", "ptz", "events"})
                onvif[field] = current.at(field);
            if (current.contains("channel_token"))
                onvif["channel_token"] = current.at("channel_token");
        }
        if (!body.contains("onvif_selection")) {
            auto saved = db.exec("SELECT settings->'onvif_control' FROM bacuri_inventory.configuration_versions "
                                 "WHERE site_id=$1 "
                                 "AND device_id=$2 AND revision=$3",
                                 {site, device, current.at("desired_revision")})
                             .get(0);
            if (!saved.empty())
                body["onvif_control"] = Json::parse(saved);
        }
        auto tag = req[http::field::if_match];
        if (tag.empty()) {
            throw Failure{428, "precondition_required"};
        }
        if (tag != etag(key_, current)) {
            res.set("ETag", etag(key_, current));
            throw Failure{412, "revision_conflict", {{"current", current}}};
        }
        if (body.contains("point_id") && body["point_id"] != current.at("point_id")) {
            throw Failure{400, "invalid_request"};
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
            throw Failure{400, "camera_transport_denied"};
        }
        if (body.contains("onvif_selection")) {
            if (!create) {
                selected = read_camera_ticket(key_, site, body.at("onvif_selection"));
                const auto id = selected.at("endpoint_id").get<std::string>();
                if (!onvif.empty() && onvif["endpoint_id"] != id)
                    throw Failure{409, "camera_binding_conflict"};
            }
            auto config =
                inventory::channel_configuration(key_, selected, body.at("main_url"), body.at("secondary_url"));
            if (!create) {
                auto match = inventory::registered_channel(db, site, config);
                if (!match.is_null() && match.at("id") != device)
                    throw Failure{409, "camera_already_registered"};
                if (!onvif.empty() && match.is_null())
                    throw Failure{409, "camera_binding_conflict"};
            }
            onvif = config.at("onvif");
            body["onvif_control"] = config.at("onvif_control");
        }
        if (!onvif.empty())
            body["onvif"] = std::move(onvif);
        inventory::write_version(db, key_, site, device, revision, body);
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
