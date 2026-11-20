#include "control/api.hpp"

namespace bacuri::vms::control
{
namespace
{

std::string etag(const Key& key, const Json& camera)
{
    return "\"" + fingerprint(key, "camera-etag", camera.dump()) + "\"";
}

Json camera(Database& db, const std::string& site, const std::string& device)
{
    auto rows = db.exec("SELECT body FROM camera_state WHERE site_id=$1 AND device_id=$2", {site, device});
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
    auto rows =
        db.exec("SELECT role FROM site_roles WHERE site_id=$1 AND user_id=$2 FOR SHARE", {site, identity.user});
    if (!rows.size()) {
        throw Failure{404, "not_found"};
    }
    return rows.get(0);
}

Json read_configuration(const Request& request)
{
    auto body = object_body(request, {"name", "main_url", "secondary_url", "point_id", "credentials"});
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
    return body;
}

void write_version(Database& db, const Key& key, const std::string& site, const std::string& device,
                   const std::string& revision, const Json& body)
{
    Json settings{{"main_url", body.at("main_url")}, {"secondary_url", body.at("secondary_url")}};
    db.exec("INSERT INTO configuration_versions VALUES ($1,$2,$3,$4)", {site, device, revision, settings.dump()});
    db.exec(R"sql(
        INSERT INTO configuration_progress(site_id,device_id,desired_revision,application_state)
        VALUES ($1,$2,$3,'pending') ON CONFLICT (site_id,device_id) DO UPDATE
        SET desired_revision=excluded.desired_revision,application_state='pending',failure_code=NULL
    )sql",
            {site, device, revision});
    if (body.contains("credentials")) {
        const auto& credentials = body.at("credentials");
        if (credentials.is_null()) {
            db.exec("UPDATE camera_registry SET secret_revision=NULL,sealed_credentials=NULL WHERE site_id=$1 AND "
                    "device_id=$2",
                    {site, device});
        } else {
            const auto context = site + "/" + device + "/" + revision;
            db.exec("UPDATE camera_registry SET secret_revision=$3,sealed_credentials=$4 WHERE site_id=$1 AND "
                    "device_id=$2",
                    {site, device, revision, seal(key, context, credentials.dump())});
        }
    }
    db.exec(R"sql(
        INSERT INTO recorder_delivery(site_id,desired_revision) VALUES ($1,1)
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
        if (!db.exec("SELECT 1 FROM observation_points WHERE site_id=$1 AND point_id=$2", {site, point}).size()) {
            throw Failure{404, "not_found"};
        }
        if (db.exec("SELECT 1 FROM camera_registry WHERE site_id=$1 AND point_id=$2", {site, point}).size()) {
            throw Failure{400, "invalid_request"};
        }
    } else {
        point = db.exec("INSERT INTO observation_points VALUES ($1,gen_random_uuid(),$2) RETURNING point_id",
                        {site, name})
                    .get(0);
    }
    auto device =
        db.exec("INSERT INTO devices VALUES ($1,gen_random_uuid(),$2) RETURNING device_id", {site, name}).get(0);
    auto channel =
        db.exec("INSERT INTO channels VALUES ($1,gen_random_uuid(),$2,0) RETURNING channel_id", {site, device})
            .get(0);
    db.exec("INSERT INTO stream_profiles SELECT $1,gen_random_uuid(),$2,role FROM "
            "unnest(ARRAY['main','secondary']) role",
            {site, channel});
    db.exec(R"sql(
        INSERT INTO point_bindings SELECT $1,$2,coalesce(max(binding_revision),0)+1,$3,
        greatest((extract(epoch FROM clock_timestamp())*1000000)::bigint,max(valid_from_utc_us)+1)
        FROM point_bindings WHERE site_id=$1 AND point_id=$2
    )sql",
            {site, point, channel});
    db.exec("INSERT INTO camera_registry(site_id,device_id,point_id) VALUES ($1,$2,$3)", {site, device, point});
    return device;
}
} // namespace

void CameraRegistry::points(const Request& req, Response& res, const std::string& site) const
{
    Database db;
    db.exec("BEGIN");
    const auto identity = sessions_.authenticate(db, req, false, false);
    const auto role = site_role(db, identity, site);
    auto rows = db.exec(R"sql(
            SELECT coalesce(jsonb_agg(jsonb_build_object('id',p.point_id,'name',p.name,
              'connection','unknown','recording','unknown') ORDER BY p.point_id),'[]')
            FROM observation_points p WHERE site_id=$1 AND ($2='administrator' OR EXISTS
              (SELECT 1 FROM point_grants g WHERE g.site_id=p.site_id AND g.point_id=p.point_id AND g.user_id=$3))
        )sql",
                        {site, role, identity.user});
    db.exec("COMMIT");
    respond(res, Json::parse(rows.get(0)));
}

void CameraRegistry::cameras(const Request& req, Response& res, const std::string& site, std::string device) const
{
    const bool create = req.method() == http::verb::post;
    const bool write = create || req.method() == http::verb::put;
    Database db;
    db.exec("BEGIN");
    const auto identity = sessions_.authenticate(db, req, write);
    if (site_role(db, identity, site) != "administrator") {
        throw Failure{403, "forbidden"};
    }
    if (!device.empty() && !canonical_id(device)) {
        throw Failure{404, "not_found"};
    }
    if (!write) {
        if (device.empty()) {
            auto rows = db.exec(
                "SELECT coalesce(jsonb_agg(body ORDER BY device_id),'[]') FROM camera_state WHERE site_id=$1",
                {site});
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
    db.exec("SELECT site_id FROM sites WHERE site_id=$1 FOR UPDATE", {site});
    const auto request_key = std::string{req["Idempotency-Key"]};
    std::string digest;
    std::string revision = "1";
    Json result;
    if (create) {
        if (!canonical_id(request_key)) {
            throw Failure{400, "invalid_request"};
        }
        digest = fingerprint(key_, "registration", body.dump());
        db.exec("DELETE FROM registration_requests WHERE site_id=$1 AND expires_at<=now()", {site});
        auto replay = db.exec("SELECT fingerprint,response FROM registration_requests WHERE site_id=$1 AND "
                              "user_id=$2 AND request_key=$3",
                              {site, identity.user, request_key});
        if (replay.size()) {
            if (replay.get(0) != digest) {
                throw Failure{409, "idempotency_conflict"};
            }
            result = Json::parse(replay.get(0, 1));
            device = result.at("id").get<std::string>();
        } else {
            device = register_device(db, site, body);
        }
    } else {
        auto current = camera(db, site, device);
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
        revision =
            db.exec("SELECT desired_revision+1 FROM configuration_progress WHERE site_id=$1 AND device_id=$2",
                    {site, device})
                .get(0);
        db.exec("UPDATE devices SET name=$3 WHERE site_id=$1 AND device_id=$2",
                {site, device, body.at("name").get<std::string>()});
    }
    if (result.is_null()) {
        if (!networks_.permits(body.at("main_url").get_ref<const std::string&>()) ||
            !networks_.permits(body.at("secondary_url").get_ref<const std::string&>())) {
            throw Failure{400, "invalid_request"};
        }
        write_version(db, key_, site, device, revision, body);
        result = camera(db, site, device);
        db.exec(R"sql(
                INSERT INTO configuration_audit(request_id,user_id,site_id,device_id,action,previous_revision,revision,outcome)
                VALUES ($6,$1,$2,$3,$4,nullif($5::bigint-1,0),$5,'saved')
            )sql",
                {identity.user, site, device, create ? "register" : "update", revision,
                 std::string{res["X-Request-Id"]}});
        if (create) {
            db.exec("INSERT INTO registration_requests VALUES ($1,$2,$3,$4,$5,now()+interval '24 hours')",
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
