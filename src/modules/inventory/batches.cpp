#include "modules/inventory/registry.hpp"
#include "modules/inventory/enrollment.hpp"
#include "modules/inventory/operations.hpp"
#include "modules/access/operations.hpp"
#include "modules/inventory/device.hpp"

namespace bacuri::vms::control
{
namespace
{
Json public_settings(const Json& config)
{
    return {{"name", config.at("name")},
            {"main_url", config.at("main_url")},
            {"secondary_url", config.at("secondary_url")}};
}

void refresh_receipt(postgresql::Database& db, const std::string& site, Json& receipt)
{
    const auto& camera = receipt.at("camera");
    auto rows = db.exec(
        "SELECT body FROM bacuri_inventory.camera_state WHERE site_id=$1 AND "
        "(device_id=nullif($2,'')::uuid OR ($2='' AND body->>'endpoint_id'=$3 AND "
        "body->>'channel_token'=$4))",
        {site, receipt.value("device_id", std::string{}), camera.at("endpoint_id"), camera.at("channel_token")});
    receipt["current"] = nullptr;
    if (rows.size()) {
        auto current = Json::parse(rows.get(0));
        Json summary = public_settings(current);
        for (const auto* k :
             {"id", "point_id", "binding_revision", "desired_revision", "applied_revision", "state"})
            summary[k] = current.at(k);
        receipt["current"] = std::move(summary);
    }
}

struct Preview {
    Json plan, receipt;
};

Json latest_template(postgresql::Database& db, const std::string& site, const std::string& id)
{
    auto rows = db.exec("SELECT settings || jsonb_build_object('id',template_id,'revision',revision::text) FROM "
                        "bacuri_inventory.camera_templates "
                        "WHERE site_id=$1 AND template_id=$2 ORDER BY revision DESC LIMIT 1",
                        {site, id});
    return rows.size() ? Json::parse(rows.get(0)) : Json(nullptr);
}

Json template_settings(postgresql::Database& db, const std::string& site, const Json& body)
{
    if (body.contains("template_id")) {
        const auto id = string_field(body, "template_id", 36);
        if (!canonical_id(id) || body.contains("record_quality") || body.contains("brightness"))
            throw Failure{ErrorCode::invalid_request};
        auto saved = latest_template(db, site, id);
        if (saved.is_null())
            throw Failure{ErrorCode::not_found};
        if (saved.at("revision") != string_field(body, "template_revision", 19))
            throw Failure{ErrorCode::revision_conflict};
        return saved;
    }
    Json settings = Json::object();
    if (body.contains("template_revision"))
        throw Failure{ErrorCode::invalid_request};
    if (body.contains("record_quality")) {
        const auto quality = string_field(body, "record_quality", 8);
        if (quality != "high" && quality != "low")
            throw Failure{ErrorCode::invalid_request};
        settings["record_quality"] = quality;
    }
    if (body.contains("brightness")) {
        if (!body.at("brightness").is_number())
            throw Failure{ErrorCode::invalid_request};
        settings["brightness"] = body.at("brightness");
    }
    return settings;
}

Json resolve_profiles(const Key& key, const std::string& site, const Json& body, std::string_view record_quality)
{
    const auto selection =
        device_family_named("onvif").read_selection(key, site, string_field(body, "selection", 24576));
    if (record_quality.empty()) {
        if (body.contains("channel"))
            throw Failure{ErrorCode::invalid_request};
        return inventory::channel_configuration(key, selection, string_field(body, "main_url", 2048),
                                                string_field(body, "secondary_url", 2048));
    }
    if (body.contains("main_url") || body.contains("secondary_url"))
        throw Failure{ErrorCode::invalid_request};
    const auto channel = string_field(body, "channel", 256, true);
    std::vector<std::pair<std::int64_t, std::string>> options;
    for (const auto& profile : selection.at("profiles")) {
        if (profile.value("channel", std::string{}) == channel && profile.value("pixels", 0LL) > 0)
            options.emplace_back(profile.at("pixels").get<std::int64_t>(),
                                 profile.at("stream_url").get<std::string>());
    }
    if (options.size() < 2)
        throw Failure{ErrorCode::camera_incompatible_profiles};
    std::ranges::sort(options);
    auto& high = options.back().second;
    auto& low = options.front().second;
    const bool record_high = record_quality == "high";
    return inventory::channel_configuration(key, selection, record_high ? high : low, record_high ? low : high);
}

Preview preview(postgresql::Database& db, const Key& key, const CameraNetworks& networks, const std::string& site,
                const Json& body)
{
    const auto settings = template_settings(db, site, body);
    auto config = resolve_profiles(key, site, body, settings.value("record_quality", std::string{}));
    const auto& meta = config.at("onvif");
    const auto& control = config.at("onvif_control");
    if (!control.contains("device_service"))
        throw Failure{ErrorCode::camera_selection_expired};
    const auto username = string_field(body, "username", 128);
    const auto password = string_field(body, "password", 1024, true);
    config["credentials"] = {{"username", username}, {"password", password}};
    config["name"] = string_field(body, "name", 256);
    if (config.at("name").get_ref<const std::string&>().find_first_not_of(" \t\n\r") == std::string::npos)
        throw Failure{ErrorCode::invalid_request};
    auto current = inventory::registered_channel(db, site, config);
    Json previous = nullptr;
    if (!current.is_null()) {
        auto rows = db.exec("SELECT v.settings,r.secret_revision,r.sealed_credentials FROM "
                            "bacuri_inventory.configuration_versions v "
                            "JOIN bacuri_inventory.camera_registry r USING(site_id,device_id) WHERE v.site_id=$1 "
                            "AND v.device_id=$2 AND v.revision=$3",
                            {site, current.at("id"), current.at("desired_revision")});
        previous = Json::parse(rows.get(0));
        previous["name"] = current.at("name");
        previous["credentials"] = nullptr;
        if (!rows.get(0, 2).empty())
            previous["credentials"] = Json::parse(unseal(
                rows.get(0, 2), key, site + "/" + current.at("id").get<std::string>() + "/" + rows.get(0, 1)));
    }
    if (body.contains("point_id")) {
        auto point = string_field(body, "point_id", 36);
        if (!canonical_id(point) || !inventory::point_exists(db, site, point))
            throw Failure{ErrorCode::invalid_request};
        if ((!current.is_null() && current.at("point_id") != point) ||
            (current.is_null() &&
             db.exec("SELECT FROM bacuri_inventory.camera_registry WHERE site_id=$1 AND point_id=$2",
                     {site, point})
                 .size()))
            throw Failure{ErrorCode::camera_binding_conflict};
        config["point_id"] = point;
    }
    auto image = device_family(control).brightness(networks, control, config.at("credentials"));
    Json brightness = nullptr;
    if (settings.contains("brightness")) {
        brightness = settings.at("brightness");
        if (!brightness.is_number())
            throw Failure{ErrorCode::invalid_request};
        if (image.is_null() || brightness < image.at("min") || brightness > image.at("max"))
            throw Failure{ErrorCode::camera_image_unsupported};
    }
    Json before = current.is_null() ? Json(nullptr) : public_settings(current);
    if (!before.is_null())
        before["point_id"] = current.at("point_id");
    auto after = public_settings(config);
    after["point_id"] =
        config.value("point_id", current.is_null() ? std::string{} : current.at("point_id").get<std::string>());
    Json receipt{{"camera", meta},
                 {"before", before},
                 {"after", after},
                 {"image", image},
                 {"brightness", brightness},
                 {"state", "prepared"},
                 {"revision", nullptr},
                 {"expected_revision", current.is_null() ? Json(nullptr) : current.at("desired_revision")}};
    if (!current.is_null())
        receipt["device_id"] = current.at("id");
    if (settings.contains("id"))
        receipt["template"] = settings;
    Json plan{{"next", config},
              {"previous", previous},
              {"device", current.is_null() ? Json(nullptr) : current.at("id")}};
    return {std::move(plan), std::move(receipt)};
}

void change_channel(postgresql::Database& db, const Key& key, const CameraNetworks& networks,
                    const std::string& site, const std::string& user, const Json& plan, Json& receipt,
                    bool rollback)
{
    const bool import = plan.at("device").is_null();
    const auto& next = plan.at("next");
    auto current = inventory::registered_channel(db, site, next);
    const bool committed = !receipt.at("revision").is_null();
    const auto expected = committed ? receipt.at("revision") : receipt.at("expected_revision");
    if (expected.is_null()) {
        if (!current.is_null())
            throw Failure{ErrorCode::revision_conflict};
        if (next.contains("point_id") &&
            db.exec("SELECT FROM bacuri_inventory.camera_registry WHERE site_id=$1 AND point_id=$2",
                    {site, next.at("point_id")})
                .size())
            throw Failure{ErrorCode::camera_binding_conflict};
    } else if (current.is_null() || current.at("desired_revision") != expected ||
               current.at("id") != (committed ? receipt.at("device_id") : plan.at("device")))
        throw Failure{ErrorCode::revision_conflict};

    const auto next_control = next.value("control", Json::object());
    const auto& family = device_family(next_control);
    family.verify_channel(networks, next);
    if (!receipt.at("brightness").is_null()) {
        const auto before = receipt.at("image").at("value").get<double>();
        const auto after = receipt.at("brightness").get<double>();
        receipt["observed_brightness"] = family.apply_brightness(
            networks, next, {.expected = rollback ? after : before, .requested = rollback ? before : after});
    }
    if (rollback && !committed)
        return;
    db.exec("SELECT bacuri_inventory.record_camera_transport_policy($1,$2::jsonb)",
            {site, Json(networks.legacy_addresses()).dump()});
    const std::string id =
        current.is_null() ? inventory::register_device(db, site, next) : current.at("id").get<std::string>();
    const auto revision =
        expected.is_null() ? std::string{"1"} : std::to_string(std::stoll(expected.get<std::string>()) + 1);
    if (rollback && import) {
        db.exec("DELETE FROM bacuri_inventory.camera_registry WHERE site_id=$1 AND device_id=$2", {site, id});
        db.exec(
            "UPDATE bacuri_inventory.recorder_delivery SET desired_revision=desired_revision+1 WHERE site_id=$1",
            {site});
    } else {
        const auto& settings = rollback ? plan.at("previous") : next;
        for (const auto* field : {"main_url", "secondary_url"})
            if (!networks.permits(settings.at(field).get_ref<const std::string&>()))
                throw Failure{ErrorCode::camera_transport_denied};
        db.exec("UPDATE bacuri_inventory.devices SET name=$3 WHERE site_id=$1 AND device_id=$2",
                {site, id, settings.at("name")});
        inventory::write_version(db, key, site, id, revision, settings);
    }
    db.exec("INSERT INTO "
            "bacuri_inventory.configuration_audit(request_id,user_id,site_id,device_id,action,previous_revision,"
            "revision,outcome) "
            "VALUES(gen_random_uuid(),$1,$2,$3,$4,nullif($5,'' )::bigint,$6,'saved')",
            {user, site, id, rollback ? "batch_rollback" : "batch_apply",
             expected.is_null() ? "" : expected.get<std::string>(), revision});
    receipt["device_id"] = id;
    receipt["revision"] = rollback && import ? Json(nullptr) : Json(revision);
}
} // namespace

void CameraRegistry::templates(const Request& req, Response& res, const std::string& site,
                               const std::string& id) const
{
    postgresql::Database db;
    db.exec("BEGIN");
    access::lock_policy(db, site, true);
    auto user = sessions_.authenticate(db, req, !id.empty(), !id.empty()).user;
    if (access::site_role(db, site, user, access::RoleLock::shared) != "administrator")
        throw Failure{ErrorCode::forbidden};
    if (id.empty()) {
        auto rows =
            db.exec("SELECT DISTINCT ON(template_id) settings || "
                    "jsonb_build_object('id',template_id,'revision',revision::text) "
                    "FROM bacuri_inventory.camera_templates WHERE site_id=$1 ORDER BY template_id,revision DESC",
                    {site});
        Json result = Json::array();
        for (const auto& row : rows.column())
            result.push_back(Json::parse(row));
        db.exec("COMMIT");
        respond(res, result);
        return;
    }
    auto body = object_body(req, {"expected_revision", "name", "record_quality", "brightness"});
    auto expected = string_field(body, "expected_revision", 19);
    auto name = string_field(body, "name", 128);
    if (name.find_first_not_of(" \t\r\n") == name.npos || !body.contains("record_quality"))
        throw Failure{ErrorCode::invalid_request};
    auto settings = template_settings(db, site, body);
    settings["name"] = name;
    inventory::lock_site(db, site);
    auto current = latest_template(db, site, id);
    const std::string version = current.is_null() ? "0" : current.at("revision").get<std::string>();
    if (version == expected) {
        db.exec("INSERT INTO bacuri_inventory.camera_templates(site_id,template_id,revision,user_id,settings) "
                "VALUES($1,$2, $3::bigint+1,$4,$5)",
                {site, id, version, user, settings.dump()});
        current = latest_template(db, site, id);
    } else {
        if (current.is_null())
            throw Failure{ErrorCode::revision_conflict};
        auto saved = current;
        saved.erase("id");
        saved.erase("revision");
        if (saved != settings || expected != std::to_string(std::stoll(version) - 1))
            throw Failure{ErrorCode::revision_conflict};
    }
    db.exec("COMMIT");
    respond(res, current);
}

void CameraRegistry::batch(const Request& req, Response& res, const std::string& site, const std::string& batch,
                           const std::string& item, std::string_view action) const
{
    postgresql::Database db;
    db.exec("BEGIN");
    auto administrator = [&] {
        access::lock_policy(db, site, true);
        auto identity =
            sessions_.authenticate(db, req, req.method() != http::verb::get, req.method() != http::verb::get);
        if (access::site_role(db, site, identity.user, access::RoleLock::shared) != "administrator")
            throw Failure{ErrorCode::forbidden};
        return identity.user;
    };
    const auto user = administrator();
    if (batch.empty()) {
        auto recent = db.exec(
            "SELECT jsonb_build_object('id',batch_id,'count',count(*),'name',(array_agg(receipt->'after'->>'name' "
            "ORDER BY created_at,item_id))[1]) "
            "FROM bacuri_inventory.camera_batch_items WHERE site_id=$1 AND user_id=$2 GROUP BY batch_id ORDER BY "
            "max(created_at) DESC,batch_id LIMIT 100",
            {site, user});
        Json list = Json::array();
        for (const auto& row : recent.column())
            list.push_back(Json::parse(row));
        db.exec("COMMIT");
        respond(res, list);
        return;
    }
    if (item.empty()) {
        auto rows = db.exec("SELECT receipt FROM bacuri_inventory.camera_batch_items WHERE site_id=$1 AND "
                            "user_id=$2 AND batch_id=$3 ORDER BY item_id",
                            {site, user, batch});
        Json result = Json::array();
        for (auto& saved : rows.column()) {
            auto receipt = Json::parse(saved);
            refresh_receipt(db, site, receipt);
            result.push_back(std::move(receipt));
        }
        db.exec("COMMIT");
        respond(res, result);
        return;
    }
    inventory::lock_site(db, site);
    auto rows = db.exec("SELECT fingerprint,sealed_plan,receipt FROM bacuri_inventory.camera_batch_items "
                        "WHERE site_id=$1 AND user_id=$2 AND batch_id=$3 AND item_id=$4 FOR UPDATE",
                        {site, user, batch, item});
    const std::string context = "camera-batch/" + site + "/" + user + "/" + batch + "/" + item;
    Json receipt;
    if (req.method() == http::verb::put) {
        auto body =
            object_body(req, {"selection", "username", "password", "name", "main_url", "secondary_url", "point_id",
                              "brightness", "channel", "record_quality", "template_id", "template_revision"});
        auto digest = fingerprint(key_, context, body.dump());
        if (rows.size()) {
            if (rows.get(0) != digest)
                throw Failure{ErrorCode::idempotency_conflict};
            receipt = Json::parse(rows.get(0, 2));
        } else {
            if (std::stoi(db.exec("SELECT count(*) FROM bacuri_inventory.camera_batch_items WHERE site_id=$1 AND "
                                  "user_id=$2 AND batch_id=$3",
                                  {site, user, batch})
                              .get(0)) >= 64)
                throw Failure{ErrorCode::camera_batch_limit};
            auto draft = preview(db, key_, networks_, site, body);
            receipt = std::move(draft.receipt);
            receipt["item_id"] = item;
            db.exec("INSERT INTO "
                    "bacuri_inventory.camera_batch_items(site_id,user_id,batch_id,item_id,fingerprint,sealed_plan,"
                    "receipt) VALUES($1,$2,$3,$4,$5,$6,$7)",
                    {site, user, batch, item, digest, seal(key_, context, draft.plan.dump()), receipt.dump()});
        }
    } else {
        if (!rows.size())
            throw Failure{ErrorCode::not_found};
        receipt = Json::parse(rows.get(0, 2));
        const bool rollback = action == "rollback";
        const auto state = receipt.at("state");
        if (!rollback && state == "saved") {
            auto retry = db.exec(
                "UPDATE bacuri_inventory.configuration_progress p SET "
                "application_state='pending',failure_code=NULL "
                "FROM bacuri_inventory.camera_registry r WHERE (p.site_id,p.device_id)=(r.site_id,r.device_id) "
                "AND p.site_id=$1 AND p.device_id=$2 AND p.desired_revision=$3 AND p.application_state='failed' "
                "RETURNING 1",
                {site, receipt.at("device_id"), receipt.at("revision")});
            if (retry.size())
                db.exec("UPDATE bacuri_inventory.recorder_delivery SET desired_revision=desired_revision+1 WHERE "
                        "site_id=$1",
                        {site});
        } else if (state != "rolled_back" &&
                   ((rollback && state != "prepared") ||
                    (!rollback && (state == "prepared" || state == "failed" || state == "applying")))) {
            const std::string intent = rollback ? "rolling_back" : "applying";
            // Persist intent before an external write. Replays compare the observed value and the original
            // version.
            receipt["state"] = intent;
            db.exec("UPDATE bacuri_inventory.camera_batch_items SET receipt=$5 WHERE site_id=$1 AND user_id=$2 "
                    "AND batch_id=$3 AND item_id=$4",
                    {site, user, batch, item, receipt.dump()});
            db.exec("COMMIT");
            db.exec("BEGIN");
            if (administrator() != user)
                throw Failure{ErrorCode::forbidden};
            inventory::lock_site(db, site);
            receipt = Json::parse(db.exec("SELECT receipt FROM bacuri_inventory.camera_batch_items WHERE "
                                          "site_id=$1 AND user_id=$2 AND batch_id=$3 AND item_id=$4 FOR UPDATE",
                                          {site, user, batch, item})
                                      .get(0));
            if (receipt.at("state") != intent) {
                refresh_receipt(db, site, receipt);
                db.exec("COMMIT");
                respond(res, receipt);
                return;
            }
            auto plan = Json::parse(unseal(rows.get(0, 1), key_, context));
            db.exec("SAVEPOINT camera_change");
            try {
                change_channel(db, key_, networks_, site, user, plan, receipt, rollback);
                receipt["state"] = rollback ? "rolled_back" : "saved";
                receipt.erase("error");
            } catch (const Failure& e) {
                db.exec("ROLLBACK TO SAVEPOINT camera_change");
                receipt["state"] = rollback ? "rollback_failed" : "failed";
                receipt["error"] = std::string{e.code()};
            }
        } else if (rollback && state == "prepared") {
            receipt["state"] = "rolled_back";
        }
        db.exec("UPDATE bacuri_inventory.camera_batch_items SET receipt=$5 WHERE site_id=$1 AND user_id=$2 AND "
                "batch_id=$3 AND item_id=$4",
                {site, user, batch, item, receipt.dump()});
    }
    refresh_receipt(db, site, receipt);
    db.exec("COMMIT");
    respond(res, receipt);
}
} // namespace bacuri::vms::control
