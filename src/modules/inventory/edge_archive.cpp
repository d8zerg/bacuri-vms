#include "modules/inventory/registry.hpp"
#include "modules/access/operations.hpp"
#include "modules/inventory/device.hpp"
#include "modules/recordings/operations.hpp"
#include "modules/recordings/recorder.hpp"
#include "application/control_api/playback_time.hpp"
#include "media/lease.hpp"
#include "platform/rpc_status.hpp"

namespace bacuri::vms::control
{
void CameraRegistry::previous_recording(const Request& req, Response& res, const std::string& site,
                                        const std::string& device) const
{
    postgresql::Database db;
    db.exec("BEGIN");
    auto identity = sessions_.authenticate(db, req, false);
    if (site_role(db, identity, site) != "administrator")
        throw Failure{ErrorCode::forbidden};
    const auto current = camera(db, site, device);
    const auto recording =
        recordings::previous_binding_recording(db, site, current.at("point_id"), current.at("binding_revision"));
    if (!recording)
        throw Failure{ErrorCode::not_found};
    db.exec("COMMIT");
    respond(res, {{"binding_revision", recording->revision},
                  {"interval", Window{recording->start, recording->end}.json()}});
}

void CameraRegistry::edge_archive(const Request& req, Response& res, const std::string& site,
                                  const std::string& device) const
{
    postgresql::Database db;
    db.exec("BEGIN");
    const bool recover = req.method() == http::verb::post;
    auto identity = sessions_.authenticate(db, req, recover);
    if (site_role(db, identity, site) != "administrator")
        throw Failure{ErrorCode::forbidden};
    auto row = db.exec(
        "SELECT v.settings->'onvif_control', "
        "r.secret_revision,r.sealed_credentials,r.point_id,b.binding_revision,b.valid_from_utc_us "
        "FROM bacuri_inventory.camera_registry r JOIN bacuri_inventory.configuration_progress p "
        "USING(site_id,device_id) "
        "JOIN bacuri_inventory.configuration_versions v ON "
        "(v.site_id,v.device_id,v.revision)=(r.site_id,r.device_id,p.desired_revision) "
        "JOIN LATERAL (SELECT binding_revision,valid_from_utc_us FROM bacuri_inventory.point_bindings WHERE "
        "site_id=r.site_id AND point_id=r.point_id ORDER BY binding_revision DESC LIMIT 1) b ON true "
        "WHERE r.site_id=$1 AND r.device_id=$2 FOR SHARE OF r",
        {site, device});
    if (!row.size())
        throw Failure{ErrorCode::not_found};
    const auto point = row.get(0, 3), binding = row.get(0, 4);
    EdgeSelection selection;
    const auto since = std::stoll(row.get(0, 5));
    Window range{since, g_get_real_time() - 15000000};
    if (recover) {
        auto body = object_body(req, {"recording", "binding_revision", "interval"});
        if (!body.contains("interval"))
            throw Failure{ErrorCode::invalid_request};
        range = window(body.at("interval"));
        selection.id = string_field(body, "recording", 64);
        if (selection.id.size() != 64 || selection.id.find_first_not_of("0123456789abcdef") != selection.id.npos)
            throw Failure{ErrorCode::invalid_request};
        if (string_field(body, "binding_revision", 19) != binding || range.start < since ||
            range.end > g_get_real_time() - 15000000)
            throw Failure{ErrorCode::camera_binding_conflict};
    }
    auto gaps = recordings::edge_gaps(db, site, point, binding, range.start, range.end);
    Json result{{"supported", false}, {"recordings", Json::array()}};
    if (!row.get(0).empty() && row.get(0) != "null" && !row.get(0, 2).empty()) {
        auto credentials = camera_credentials(
            Json::parse(unseal(row.get(0, 2), key_, site + "/" + device + "/" + row.get(0, 1))));
        const auto control = camera_control(Json::parse(row.get(0)));
        result = device_family(control).read_edge_archive(networks_, control, credentials, key_,
                                                          recover ? &selection : nullptr);
    }
    if (!recover) {
        result["binding_revision"] = binding;
        result["gaps"] = Json::array();
        for (const auto& gap : gaps)
            result["gaps"].push_back(Window{gap.begin, gap.end}.json());
    } else {
        if (selection.uri.empty())
            throw Failure{ErrorCode::edge_recording_unavailable};
        Json progress{{"blocks", 0}, {"duration_us", 0}, {"outcome", "no_frames"}, {"next", nullptr}};
        for (const auto& gap : gaps) {
            bool attempted = false;
            for (const auto& record : result.at("recordings")) {
                if (record.at("id") != selection.id)
                    continue;
                for (const auto& track : record.at("tracks")) {
                    if (track.at("kind") != "Video" || track.at("bounds").is_null())
                        continue;
                    const auto a =
                        std::max(gap.begin, parse_utc(track.at("bounds").at("start")) + selection.offset_us);
                    const auto b = std::min(
                        {gap.end, parse_utc(track.at("bounds").at("end")) + selection.offset_us, a + 15000000});
                    if (a >= b)
                        continue;
                    v1::EdgeReplayRequest q;
                    q.mutable_point()->set_site_id(site);
                    q.mutable_point()->set_point_id(point);
                    q.set_device_id(device);
                    q.set_binding(std::stoll(binding));
                    q.set_recording_id(selection.id);
                    q.set_uri(selection.uri);
                    q.set_begin_us(a);
                    q.set_end_us(b);
                    q.set_offset_us(selection.offset_us);
                    q.set_uncertainty_us(selection.uncertainty_us);
                    v1::EdgeReplayResult reply;
                    grpc::ClientContext ctx;
                    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds{35});
                    auto status = v1::RecorderControl::NewStub(media::local_channel("BACURI_SOCKET"))
                                      ->RecoverGap(&ctx, q, &reply);
                    if (const auto cause = platform::rpc_problem(status))
                        throw Failure{*cause};
                    if (status.error_code() == grpc::StatusCode::RESOURCE_EXHAUSTED)
                        throw Failure{ErrorCode::rate_limited};
                    if (!status.ok())
                        throw Failure{ErrorCode::media_service_unavailable};
                    // A partial copy continues after the copied media, so the rest of the window is retried.
                    const auto resume = reply.outcome() == "partial" && reply.covered_until_us() > a &&
                                                reply.covered_until_us() < b
                                            ? reply.covered_until_us()
                                            : b;
                    progress = {{"blocks", reply.blocks()},
                                {"duration_us", reply.duration_us()},
                                {"outcome", reply.outcome()},
                                {"next", resume < range.end ? Json(utc_text(resume)) : Json(nullptr)}};
                    attempted = true;
                    break;
                }
            }
            if (attempted)
                break;
        }
        result = progress;
    }
    db.exec("COMMIT");
    respond(res, result);
}
} // namespace bacuri::vms::control
