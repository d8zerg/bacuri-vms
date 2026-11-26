#include "application/control_api/http_json.hpp"
#include "modules/inventory/device_configuration_json.hpp"
#include "modules/inventory/camera_events.hpp"
#include "modules/inventory/device.hpp"
#include "modules/events/observations.hpp"
#include <cstdio>
#include <map>
#include <set>
#include <thread>

namespace bacuri::vms::control
{
namespace
{
using namespace std::chrono_literals;
struct CameraReader {
    std::string site, device, point;
    std::int64_t binding, revision, bound_since;
    CameraControl control;
    CameraCredentials credentials;
};
void delay(const std::stop_token& stop, int ticks)
{
    for (int i = 0; i < ticks && !stop.stop_requested(); ++i)
        std::this_thread::sleep_for(100ms);
}
void listen(std::stop_token stop, const CameraNetworks& networks, const Key& key, const CameraReader& camera)
{
    auto accept = [&](const MotionNotice& notice) {
        if (notice.utc + notice.uncertainty < camera.bound_since && notice.uncertainty <= 5000000)
            return;
        if (!notice.input.empty()) {
            postgresql::Database db;
            db.exec("INSERT INTO bacuri_inventory.digital_input_states AS "
                    "previous(site_id,device_id,revision,token,active,source_time) "
                    "VALUES($1,$2,$3,$4,$5,$6) ON CONFLICT(site_id,device_id,revision,token) DO UPDATE SET "
                    "active=excluded.active,source_time=excluded.source_time,observed_at=clock_timestamp() "
                    "WHERE excluded.source_time>=previous.source_time",
                    {camera.site, camera.device, std::to_string(camera.revision), notice.input,
                     notice.active ? "true" : "false", std::to_string(notice.utc)});
            return;
        }
        auto hash = fingerprint(key, "onvif-motion",
                                Json::array({camera.device, camera.binding, notice.source, notice.utc}).dump() +
                                    (notice.topic == "RuleEngine/CellMotionDetector/Motion" ? "" : notice.topic));
        v1::SubmitObservationRequest q;
        q.set_site_id(camera.site);
        q.set_source_id(camera.device);
        q.set_observation_id(hash.substr(0, 8) + "-" + hash.substr(8, 4) + "-8" + hash.substr(13, 3) + "-a" +
                             hash.substr(17, 3) + "-" + hash.substr(20, 12));
        q.set_type("motion");
        q.set_schema_version(1);
        q.mutable_binding()->set_point_id(camera.point);
        q.mutable_binding()->set_revision(camera.binding);
        q.set_source_time_us(notice.utc);
        if (notice.uncertainty >= 0 && notice.uncertainty <= 5000000) {
            q.mutable_clock()->set_epoch_id(notice.epoch);
            q.mutable_clock()->set_uncertainty_us(static_cast<std::uint64_t>(notice.uncertainty));
        }
        q.set_attributes_json(
            Json{{"topic", notice.topic}, {"source", notice.source}, {"data", notice.metadata}}.dump());
        while (!stop.stop_requested()) {
            v1::RegistrationResult result;
            auto status = store_observation(q, result, camera.revision);
            if (status.error_code() != grpc::StatusCode::UNAVAILABLE)
                break;
            delay(stop, 5);
        }
    };
    while (!stop.stop_requested()) {
        try {
            device_family(camera.control).read_motion(stop, networks, camera.control, camera.credentials, accept);
        } catch (const Failure& e) {
            std::fprintf(stderr, "ONVIF events %s %s\n", camera.device.c_str(), std::string{e.code()}.c_str());
            delay(stop, 20);
        } catch (const std::exception&) {
            delay(stop, 20);
        }
    }
}
} // namespace

void camera_events(const std::stop_token& stop, const CameraNetworks& networks, const Key& key)
{
    const auto site = setting("BACURI_SITE_ID");
    std::map<std::string, std::jthread> readers;
    while (!stop.stop_requested()) {
        try {
            postgresql::Database db;
            auto rows = db.exec(R"sql(
                SELECT r.device_id,r.point_id,b.binding_revision,p.desired_revision,v.settings->'onvif_control',
                       r.secret_revision,r.sealed_credentials,b.valid_from_utc_us
                FROM bacuri_inventory.camera_registry r JOIN bacuri_inventory.configuration_progress p USING(site_id,device_id)
                JOIN LATERAL (SELECT binding_revision,valid_from_utc_us FROM bacuri_inventory.point_bindings
                              WHERE site_id=r.site_id AND point_id=r.point_id ORDER BY binding_revision DESC LIMIT 1) b ON true
                JOIN bacuri_inventory.configuration_versions v ON (v.site_id,v.device_id,v.revision)=(r.site_id,r.device_id,p.desired_revision)
                WHERE r.site_id=$1 AND r.sealed_credentials IS NOT NULL AND v.settings->'onvif_control' ? 'Events'
                AND v.settings->'onvif_control'->>'source' <> ''
            )sql",
                                {site});
            std::set<std::string> active;
            for (int i = 0; i < rows.size(); ++i)
                active.insert(rows.get(i) + "/" + rows.get(i, 3));
            for (auto& [id, thread] : readers)
                if (!active.contains(id))
                    thread.request_stop();
            std::erase_if(readers, [&](const auto& reader) { return !active.contains(reader.first); });
            for (int i = 0; i < rows.size(); ++i) {
                auto id = rows.get(i) + "/" + rows.get(i, 3);
                if (readers.contains(id))
                    continue;
                CameraReader camera{site,
                                    rows.get(i),
                                    rows.get(i, 1),
                                    std::stoll(rows.get(i, 2)),
                                    std::stoll(rows.get(i, 3)),
                                    std::stoll(rows.get(i, 7)),
                                    camera_control(Json::parse(rows.get(i, 4))),
                                    camera_credentials(Json::parse(unseal(
                                        rows.get(i, 6), key, site + "/" + rows.get(i) + "/" + rows.get(i, 5))))};
                readers.try_emplace(id, [&, camera = std::move(camera)](const std::stop_token& token) {
                    listen(token, networks, key, camera);
                });
            }
        } catch (const std::exception&) {
            std::fputs("Camera event registry unavailable\n", stderr);
        }
        delay(stop, 10);
    }
    for (auto& [id, thread] : readers)
        thread.request_stop();
}
} // namespace bacuri::vms::control
