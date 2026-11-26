#include "modules/inventory/registry.hpp"
#include "modules/access/operations.hpp"
#include "modules/inventory/device.hpp"
#include "modules/events/operations.hpp"
#include "modules/inventory/telemetry.hpp"
#include "modules/inventory/operations.hpp"

namespace bacuri::vms::control
{
void CameraRegistry::ptz(const Request& req, Response& res, const std::string& site,
                         const std::string& point) const
{
    postgresql::Database db;
    db.exec("BEGIN");
    access::lock_policy(db, site, true);
    const bool write = req.method() == http::verb::post;
    auto user = sessions_.authenticate(db, req, write, write);
    auto role = site_role(db, user, site);
    const bool visible = role == "administrator" ? inventory::lock_point(db, site, point)
                                                 : access::lock_point_grant(db, site, point, user.user);
    if (!visible)
        throw Failure{ErrorCode::not_found};
    if (!access::allowed(db, site, user.user, point, "ptz")) {
        if (write) {
            events::record_camera_control(db, site, point, user.user, "denied", "forbidden", {});
            db.exec("COMMIT");
        }
        throw Failure{ErrorCode::forbidden};
    }
    const auto body = write ? object_body(req, {"device_id", "command", "target"}) : Json::object();
    const auto expected = write ? string_field(body, "device_id", 36) : "";
    const auto command = write ? string_field(body, "command", 16) : "";
    const auto target = body.contains("target") ? string_field(body, "target", 256, true) : "";
    const std::string request_key{req["Idempotency-Key"]};
    if (write && (!canonical_id(expected) || !canonical_id(request_key)))
        throw Failure{ErrorCode::invalid_request};
    auto binding_sql = std::string{R"sql(
        SELECT v.settings->'onvif_control', r.secret_revision, r.sealed_credentials,r.device_id,p.desired_revision
        FROM bacuri_inventory.camera_registry r JOIN bacuri_inventory.configuration_progress p USING(site_id,device_id)
        JOIN bacuri_inventory.configuration_versions v ON (v.site_id,v.device_id,v.revision)=(r.site_id,r.device_id,p.desired_revision)
        WHERE r.site_id=$1 AND r.point_id=$2
    )sql"} + (write ? " FOR NO KEY UPDATE OF r" : " FOR KEY SHARE OF r");
    auto rows = db.exec(binding_sql.c_str(), {site, point});
    if (!rows.size() || (write && rows.get(0, 3) != expected))
        throw Failure{ErrorCode::camera_binding_conflict};
    const auto device = rows.get(0, 3);
    if (!write) {
        if (rows.get(0).empty() || rows.get(0, 2).empty())
            throw Failure{ErrorCode::camera_ptz_unsupported};
        auto credentials = camera_credentials(
            Json::parse(unseal(rows.get(0, 2), key_, site + "/" + device + "/" + rows.get(0, 1))));
        Json catalog{{"actions", Json::array()}, {"inputs", Json::array()}};
        try {
            const auto control = camera_control(Json::parse(rows.get(0)));
            catalog = device_family(control).read_controls(networks_, control, credentials);
        } catch (const Failure& e) {
            catalog["capability_error"] = std::string{e.code()};
        }
        for (auto& item : catalog["actions"]) {
            item.erase("amount");
            item.erase("mode");
            item.erase("duration");
        }
        auto states = db.exec("SELECT token,active,observed_at FROM bacuri_inventory.digital_input_states "
                              "WHERE site_id=$1 AND device_id=$2 AND revision=$3",
                              {site, device, rows.get(0, 4)});
        for (auto& input : catalog["inputs"])
            for (int n = 0; n < states.size(); ++n)
                if (input.at("token") == states.get(n)) {
                    input["active"] = states.get(n, 1) == "t";
                    input["observed_at"] = states.get(n, 2);
                }
        catalog["device_id"] = device;
        auto history = db.exec("SELECT user_id,command,target,priority,lease_until>clock_timestamp(),"
                               "lease_until,CASE WHEN status=204 THEN 'acknowledged' WHEN status IS NULL OR "
                               "failure_code='camera_ptz_unknown' "
                               "THEN 'unknown' ELSE 'rejected' END,started_at FROM bacuri_inventory.ptz_commands "
                               "WHERE site_id=$1 AND device_id=$2 ORDER BY started_at DESC LIMIT 20",
                               {site, device});
        catalog["history"] = Json::array();
        for (int i = 0; i < history.size(); ++i) {
            auto actor = access::display_name(db, history.get(i));
            catalog["history"].push_back({{"actor", actor},
                                          {"command", history.get(i, 1)},
                                          {"target", history.get(i, 2)},
                                          {"outcome", history.get(i, 6)},
                                          {"at", history.get(i, 7)}});
            if (i == 0 && history.get(i, 4) == "t")
                catalog["owner"] = {
                    {"name", actor}, {"priority", std::stoi(history.get(i, 3))}, {"until", history.get(i, 5)}};
        }
        db.exec("COMMIT");
        return respond(res, catalog);
    }
    dispatch_ptz(rows, site, point, user.user, request_key, command, target, role);
    db.exec("COMMIT");
    res.result(http::status::no_content);
}

void CameraRegistry::preset(const std::string& site, const std::string& point, std::int64_t binding,
                            const std::string& user, const std::string& request, const std::string& target) const
{
    postgresql::Database db;
    db.exec("BEGIN");
    access::lock_policy(db, site, true);
    const auto role = access::site_role(db, site, user, access::RoleLock::shared);
    if (!access::allowed(db, site, user, point, "configure") || !access::allowed(db, site, user, point, "ptz"))
        throw Failure{ErrorCode::forbidden};
    auto rows = db.exec(
        "SELECT v.settings->'onvif_control',r.secret_revision,r.sealed_credentials,r.device_id,p.desired_revision "
        "FROM bacuri_inventory.camera_registry r JOIN bacuri_inventory.configuration_progress p "
        "USING(site_id,device_id) "
        "JOIN bacuri_inventory.configuration_versions v ON "
        "(v.site_id,v.device_id,v.revision)=(r.site_id,r.device_id,p.desired_revision) "
        "JOIN bacuri_inventory.point_bindings b ON (b.site_id,b.point_id)=(r.site_id,r.point_id) AND "
        "b.binding_revision=(SELECT max(last.binding_revision) FROM bacuri_inventory.point_bindings last WHERE "
        "last.site_id=r.site_id AND last.point_id=r.point_id) "
        "WHERE r.site_id=$1 AND r.point_id=$2 AND b.binding_revision=$3 FOR NO KEY UPDATE OF r",
        {site, point, std::to_string(binding)});
    if (!rows.size())
        throw Failure{ErrorCode::camera_binding_conflict};
    dispatch_ptz(rows, site, point, user, request, "preset_goto", target, role);
    db.exec("COMMIT");
}
void CameraRegistry::dispatch_ptz(const postgresql::Rows& rows, const std::string& site, const std::string& point,
                                  const std::string& user, const std::string& request_key,
                                  const std::string& command, const std::string& target,
                                  const std::string& role) const
{
    const auto device = rows.get(0, 3);
    // Keep the binding locked while a separate transaction records dispatch before touching the device.
    postgresql::Database journal;

    auto replay = [&] {
        auto saved = journal.exec(
            "SELECT device_id,point_id,command,status,failure_code,target FROM bacuri_inventory.ptz_commands "
            "WHERE site_id=$1 AND user_id=$2 AND request_key=$3",
            {site, user, request_key});
        if (!saved.size())
            return false;
        if (saved.get(0) != device || saved.get(0, 1) != point || saved.get(0, 2) != command ||
            saved.get(0, 5) != target)
            throw Failure{ErrorCode::idempotency_conflict};
        if (saved.get(0, 3).empty())
            throw Failure{ErrorCode::camera_ptz_unknown};
        // A stored refusal of the camera keeps its cause, a cause outside the catalog stays a protocol error.
        if (std::stoi(saved.get(0, 3)) != 204)
            throw Failure{parse_error(saved.get(0, 4)).value_or(ErrorCode::camera_protocol_error)};
        return true;
    };
    if (!replay()) {
        if (rows.get(0).empty() || rows.get(0, 2).empty())
            throw Failure{ErrorCode::camera_ptz_unsupported};
        auto control = camera_control(Json::parse(rows.get(0)));
        const auto& allowed = control.directions;
        auto credentials = camera_credentials(
            Json::parse(unseal(rows.get(0, 2), key_, site + "/" + device + "/" + rows.get(0, 1))));
        Json action;
        if (std::ranges::find(allowed, command) == allowed.end() || !target.empty()) {
            auto catalog = device_family(control).read_controls(networks_, control, credentials);
            for (const auto& entry : catalog.at("actions"))
                if (entry.at("command") == command && entry.at("target") == target) {
                    action = entry;
                    break;
                }
            if (action.is_null())
                throw Failure{ErrorCode::camera_ptz_unsupported};
        }
        const int lease = command == "stop" || command == "focus_stop" || command == "tour_stop" ? 0
                          : action.is_object() ? action.value("duration", 1)
                                               : 1;
        auto owner = journal.exec("SELECT user_id,lease_until>clock_timestamp() FROM "
                                  "bacuri_inventory.ptz_commands WHERE site_id=$1 AND device_id=$2 "
                                  "ORDER BY started_at DESC LIMIT 1",
                                  {site, device});
        if (owner.size() && owner.get(0, 1) == "t" && owner.get(0) != user && role != "administrator")
            throw Failure{ErrorCode::camera_ptz_busy};
        journal.exec("BEGIN");
        auto created = journal.exec("INSERT INTO "
                                    "bacuri_inventory.ptz_commands(site_id,user_id,request_key,device_id,point_id,"
                                    "command,target,priority,lease_until)"
                                    " "
                                    "VALUES "
                                    "($1,$2,$3,$4,$5,$6,$7,$8,clock_timestamp()+$9::int*interval '1 second') ON "
                                    "CONFLICT DO NOTHING RETURNING 1",
                                    {site, user, request_key, device, point, command, target,
                                     role == "administrator" ? "2" : "1", std::to_string(lease)});
        if (created.size()) {
            if (action.is_object() && action.value("mode", std::string{}) == "Continuous")
                journal.exec("UPDATE bacuri_inventory.ptz_commands SET "
                             "focus_revision=$4,stop_after=clock_timestamp()+interval '1 second' "
                             "WHERE site_id=$1 AND user_id=$2 AND request_key=$3",
                             {site, user, request_key, rows.get(0, 4)});
            events::record_camera_control(journal, site, point, user, "dispatched", command, target);
        }
        journal.exec("COMMIT");
        if (created.size()) {
            inventory::refresh_position(journal, site, device);
            try {
                if (action.is_null())
                    device_family(control).move(networks_, control, credentials, command);
                else
                    device_family(control).operate(
                        networks_, control, credentials,
                        {command, target, action.value("mode", std::string{}), action.value("amount", 0.0)});
            } catch (const Failure& e) {
                // A missing SOAP acknowledgement leaves the outcome unknown even when the request reached the
                // device.
                const auto failure =
                    (e.error == ErrorCode::camera_unavailable || e.error == ErrorCode::camera_protocol_error)
                        ? Failure{ErrorCode::camera_ptz_unknown}
                        : e;
                journal.exec("BEGIN");
                events::record_camera_control(
                    journal, site, point, user,
                    failure.error == ErrorCode::camera_ptz_unknown ? "unknown" : "rejected", command, target);
                journal.exec(
                    "UPDATE bacuri_inventory.ptz_commands SET status=$4,failure_code=$5 WHERE site_id=$1 "
                    "AND user_id=$2 "
                    "AND request_key=$3",
                    {site, user, request_key, std::to_string(failure.status()), std::string{failure.code()}});
                journal.exec("COMMIT");
                throw failure;
            }
            journal.exec("BEGIN");
            events::record_camera_control(journal, site, point, user, "applied", command, target);
            journal.exec("UPDATE bacuri_inventory.ptz_commands SET "
                         "status=204,stop_after=NULL,lease_until=clock_timestamp()+$4::int*interval '1 second' "
                         "WHERE site_id=$1 "
                         "AND user_id=$2 AND request_key=$3",
                         {site, user, request_key, std::to_string(lease)});
            journal.exec("COMMIT");
        } else if (!replay())
            throw Failure{ErrorCode::dependency_unavailable};
    }
}
} // namespace bacuri::vms::control
