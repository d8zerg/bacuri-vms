#include "application/control_api/http_json.hpp"
#include "modules/inventory/selection_json.hpp"
#include "modules/inventory/enrollment.hpp"
#include "modules/inventory/device.hpp"

namespace bacuri::vms::control::inventory
{
void write_version(postgresql::Database& db, const Key& key, const std::string& site, const std::string& device,
                   const std::string& revision, const CameraVersion& body)
{
    Json settings{{"main_url", body.main_url}, {"secondary_url", body.secondary_url}};
    if (body.camera)
        settings["onvif"] = *body.camera;
    if (body.control)
        settings["onvif_control"] = *body.control;
    db.exec("INSERT INTO bacuri_inventory.configuration_versions VALUES ($1,$2,$3,$4)",
            {site, device, revision, settings.dump()});
    db.exec(R"sql(
        INSERT INTO bacuri_inventory.configuration_progress(site_id,device_id,desired_revision,application_state)
        VALUES ($1,$2,$3,'pending') ON CONFLICT (site_id,device_id) DO UPDATE
        SET desired_revision=excluded.desired_revision,application_state='pending',failure_code=NULL
    )sql",
            {site, device, revision});
    if (body.update_credentials) {
        const auto& credentials = body.credentials;
        if (!credentials) {
            db.exec("UPDATE bacuri_inventory.camera_registry SET secret_revision=NULL,sealed_credentials=NULL "
                    "WHERE site_id=$1 AND "
                    "device_id=$2",
                    {site, device});
        } else {
            const auto context = site + "/" + device + "/" + revision;
            db.exec("UPDATE bacuri_inventory.camera_registry SET secret_revision=$3,sealed_credentials=$4 WHERE "
                    "site_id=$1 AND "
                    "device_id=$2",
                    {site, device, revision,
                     seal(key, context,
                          Json{{"username", credentials->username}, {"password", credentials->password}}.dump())});
        }
    }
    db.exec(R"sql(
        INSERT INTO bacuri_inventory.recorder_delivery(site_id,desired_revision) VALUES ($1,1)
        ON CONFLICT (site_id) DO UPDATE SET desired_revision=recorder_delivery.desired_revision+1
    )sql",
            {site});
}

std::string register_device(postgresql::Database& db, const std::string& site, const CameraRegistration& body)
{
    const auto& name = body.name;
    std::string point;
    if (body.point_id) {
        point = *body.point_id;
        if (!db.exec("SELECT 1 FROM bacuri_inventory.observation_points WHERE site_id=$1 AND point_id=$2",
                     {site, point})
                 .size()) {
            throw Failure{ErrorCode::not_found};
        }
        if (body.replace_device_id) {
            if (!db.exec("DELETE FROM bacuri_inventory.camera_registry WHERE site_id=$1 AND point_id=$2 AND "
                         "device_id=$3 RETURNING "
                         "device_id",
                         {site, point, *body.replace_device_id})
                     .size())
                throw Failure{ErrorCode::camera_binding_conflict};
        }
        if (db.exec("SELECT 1 FROM bacuri_inventory.camera_registry WHERE site_id=$1 AND point_id=$2",
                    {site, point})
                .size()) {
            throw Failure{ErrorCode::invalid_request};
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
SelectedChannel channel_configuration(const Key& key, const CameraSelection& selection, const std::string& main,
                                      const std::string& secondary)
{
    const auto& profiles = selection.profiles;
    auto first = profiles.find(fingerprint(key, "onvif-stream", main)),
         second = profiles.find(fingerprint(key, "onvif-stream", secondary));
    if (first == profiles.end() || second == profiles.end())
        throw Failure{ErrorCode::invalid_request};
    const auto& primary = first->second;
    if (main == secondary || !second->second.profile ||
        primary.channel.value_or("") != second->second.channel.value_or(""))
        throw Failure{ErrorCode::camera_incompatible_profiles};
    SelectedChannel result{
        selection.identity, {selection.services, primary, second->second.profile}, main, secondary, {}, {}};
    if (primary.ptz_profile)
        result.identity.ptz = result.identity.ptz && *primary.ptz_profile;
    result.identity.channel_token = primary.channel.value_or("");
    for (const auto& [id, p] : profiles)
        if (p.channel && p.stream_url) {
            if (!p.profile)
                throw Failure{ErrorCode::camera_incompatible_profiles};
            result.profile_channels[*p.profile] = *p.channel;
            result.stream_channels[*p.stream_url] = *p.channel;
        }
    return result;
}
std::optional<RegisteredCamera> registered_channel(postgresql::Database& db, const std::string& site,
                                                   const SelectedChannel& config)
{
    const auto channel = config.identity.channel_token.value_or("");
    // Older configurations identify the recorded profile, before a physical source token was stored.
    auto rows = db.exec(R"sql(
        SELECT c.body,coalesce(nullif(c.body->>'channel_token',''),$3::jsonb->>(v.settings#>>'{onvif_control,profile}'),
                               $4::jsonb->>(c.body->>'main_url'),$4::jsonb->>(c.body->>'secondary_url'),'')
        FROM bacuri_inventory.camera_state c JOIN bacuri_inventory.configuration_versions v
          ON (v.site_id,v.device_id,v.revision)=(c.site_id,c.device_id,(c.body->>'desired_revision')::bigint)
        WHERE c.site_id=$1 AND c.body->>'endpoint_id'=$2
    )sql",
                        {site, config.identity.endpoint_id, Json(config.profile_channels).dump(),
                         Json(config.stream_channels).dump()});
    std::optional<RegisteredCamera> match;
    for (int i = 0; i < rows.size(); ++i) {
        const auto source = rows.get(i, 1);
        if (!channel.empty() && source.empty())
            throw Failure{ErrorCode::camera_binding_conflict};
        if (channel.empty() || channel == source) {
            if (match)
                throw Failure{ErrorCode::camera_binding_conflict};
            match = Json::parse(rows.get(i)).get<RegisteredCamera>();
        }
    }
    return match;
}
} // namespace bacuri::vms::control::inventory
