#include "modules/inventory/enrollment.hpp"
#include "modules/inventory/device.hpp"

namespace bacuri::vms::control::inventory
{
void write_version(postgresql::Database& db, const Key& key, const std::string& site, const std::string& device,
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

std::string register_device(postgresql::Database& db, const std::string& site, const Json& body)
{
    const auto name = body.at("name").get<std::string>();
    std::string point;
    if (body.contains("point_id")) {
        point = body.at("point_id").get<std::string>();
        if (!db.exec("SELECT 1 FROM bacuri_inventory.observation_points WHERE site_id=$1 AND point_id=$2",
                     {site, point})
                 .size()) {
            throw Failure{ErrorCode::not_found};
        }
        if (body.contains("replace_device_id")) {
            if (!db.exec("DELETE FROM bacuri_inventory.camera_registry WHERE site_id=$1 AND point_id=$2 AND "
                         "device_id=$3 RETURNING "
                         "device_id",
                         {site, point, body.at("replace_device_id").get<std::string>()})
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
Json channel_configuration(const Key& key, const Json& selection, const std::string& main,
                           const std::string& secondary)
{
    const auto& profiles = selection.at("profiles");
    const auto first = profiles.find(fingerprint(key, "onvif-stream", main));
    const auto second = profiles.find(fingerprint(key, "onvif-stream", secondary));
    if (first == profiles.end() || second == profiles.end())
        throw Failure{ErrorCode::invalid_request};
    if (main == secondary || first->value("channel", std::string{}) != second->value("channel", std::string{}))
        throw Failure{ErrorCode::camera_incompatible_profiles};
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
Json registered_channel(postgresql::Database& db, const std::string& site, const Json& config)
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
            throw Failure{ErrorCode::camera_binding_conflict};
        if (channel.empty() || channel == source) {
            if (!match.is_null())
                throw Failure{ErrorCode::camera_binding_conflict};
            match = Json::parse(rows.get(i));
        }
    }
    return match;
}
} // namespace bacuri::vms::control::inventory
