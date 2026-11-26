#include "control/delivery.hpp"
#include "control/database.hpp"
#include "control.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#include <nlohmann/json.hpp>
#include <thread>

namespace bacuri::vms::control
{
namespace
{
using namespace std::chrono_literals;

struct Assignment {
    std::string site;
    std::string node;
};

v1::ApplyConfigurationRequest prepare(Database& db, const Key& key, const Assignment& assignment)
{
    const auto& [site, node] = assignment;
    db.exec("BEGIN");
    auto pending = db.exec(R"sql(
        SELECT 1 FROM bacuri_inventory.recorder_delivery WHERE site_id=$1 AND desired_revision>decided_revision
    )sql",
                           {site});
    if (!pending.size()) {
        return {};
    }
    db.exec("SELECT site_id FROM bacuri_inventory.sites WHERE site_id=$1 FOR UPDATE", {site});
    auto previous = db.exec(
        "SELECT generation,sealed_snapshot FROM bacuri_inventory.recorder_attempt WHERE site_id=$1", {site});
    if (previous.size()) {
        v1::ApplyConfigurationRequest saved;
        if (!saved.ParseFromString(unseal(previous.get(0, 1), key, "delivery/" + site + "/" + previous.get(0)))) {
            throw std::runtime_error("Invalid pending delivery");
        }
        db.exec("COMMIT");
        return saved;
    }
    const auto generation =
        db.exec("SELECT desired_revision FROM bacuri_inventory.recorder_delivery WHERE site_id=$1", {site}).get(0);
    v1::ApplyConfigurationRequest snapshot;
    snapshot.set_site_id(site);
    snapshot.set_recording_node_id(node);
    snapshot.set_revision(std::stoll(generation));
    auto rows = db.exec(R"sql(
        SELECT s.device_id,s.point_id,s.body->>'main_url',s.body->>'secondary_url',r.secret_revision,r.sealed_credentials,
               b.channel_id,p.profile_id,b.binding_revision
        FROM bacuri_inventory.camera_state s JOIN bacuri_inventory.camera_registry r USING(site_id,device_id,point_id)
        JOIN LATERAL (SELECT channel_id,binding_revision FROM bacuri_inventory.point_bindings
                      WHERE site_id=s.site_id AND point_id=s.point_id ORDER BY binding_revision DESC LIMIT 1) b ON true
        JOIN bacuri_inventory.stream_profiles p ON p.site_id=s.site_id AND p.channel_id=b.channel_id AND p.stream_role='main'
        WHERE s.site_id=$1 ORDER BY s.device_id
    )sql",
                        {site});
    for (int i = 0; i < rows.size(); ++i) {
        auto* camera = snapshot.add_cameras();
        camera->set_device_id(rows.get(i));
        camera->set_point_id(rows.get(i, 1));
        camera->set_main_url(rows.get(i, 2));
        camera->set_secondary_url(rows.get(i, 3));
        camera->set_channel_id(rows.get(i, 6));
        camera->set_main_profile_id(rows.get(i, 7));
        camera->set_binding_revision(std::stoll(rows.get(i, 8)));
        const auto secret_revision = rows.get(i, 4);
        if (!secret_revision.empty()) {
            const auto secret = nlohmann::json::parse(
                unseal(rows.get(i, 5), key,
                       std::string{site}.append("/").append(rows.get(i)).append("/").append(secret_revision)));
            camera->mutable_credentials()->set_username(secret.at("username").get<std::string>());
            camera->mutable_credentials()->set_password(secret.at("password").get<std::string>());
        }
    }
    db.exec(R"sql(
        INSERT INTO bacuri_inventory.recorder_attempt SELECT $1,$2,$3,coalesce(jsonb_object_agg(device_id,desired_revision),'{}')
        FROM bacuri_inventory.configuration_progress JOIN bacuri_inventory.camera_registry USING (site_id,device_id) WHERE site_id=$1
    )sql",
            {site, generation, seal(key, "delivery/" + site + "/" + generation, snapshot.SerializeAsString())});
    db.exec("COMMIT");
    return snapshot;
}

void send_pending(v1::RecorderControl::Stub& recorder, const Key& key, const Assignment& assignment,
                  std::optional<std::string>& policy)
{
    const auto& site = assignment.site;
    Database db;
    if (db.exec("SELECT pg_try_advisory_lock(hashtextextended($1,0))", {site}).get(0) != "t") {
        return;
    }
    if (policy) {
        db.exec("BEGIN");
        bool exists =
            db.exec("SELECT 1 FROM bacuri_inventory.sites WHERE site_id=$1 FOR UPDATE", {site}).size() > 0;
        if (exists)
            db.exec("SELECT bacuri_inventory.record_camera_transport_policy($1,$2::jsonb)", {site, *policy});
        db.exec("COMMIT");
        if (exists)
            policy.reset();
    }
    const auto snapshot = prepare(db, key, assignment);
    if (snapshot.revision() == 0) {
        return;
    }
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + 2s);
    v1::ApplyConfigurationResult decision;
    auto status = recorder.ApplyConfiguration(&context, snapshot, &decision);
    if (!status.ok() || decision.requested_revision() != snapshot.revision()) {
        return;
    }
    db.exec("BEGIN");
    db.exec("SELECT site_id FROM bacuri_inventory.sites WHERE site_id=$1 FOR UPDATE", {site});
    if (decision.state() == v1::APPLY_STATE_APPLIED && decision.applied_revision() == snapshot.revision()) {
        db.exec(R"sql(
            UPDATE bacuri_inventory.configuration_progress p SET applied_revision=r.value::bigint,
              application_state=CASE WHEN p.desired_revision=r.value::bigint THEN 'applied' ELSE 'pending' END,
              failure_code=NULL
            FROM bacuri_inventory.recorder_attempt a CROSS JOIN LATERAL jsonb_each_text(a.revisions) r
            WHERE a.site_id=$1 AND p.site_id=a.site_id AND p.device_id=r.key::uuid
        )sql",
                {site});
    } else if (decision.state() == v1::APPLY_STATE_REJECTED) {
        const auto reason = decision.failure() == v1::APPLY_FAILURE_SOURCE_UNREACHABLE ? "source_unreachable"
                            : decision.failure() == v1::APPLY_FAILURE_INCOMPATIBLE_PROFILES
                                ? "incompatible_profiles"
                                : "invalid_configuration";
        db.exec("UPDATE bacuri_inventory.configuration_progress SET application_state='failed',failure_code=$2 "
                "WHERE site_id=$1 "
                "AND applied_revision IS DISTINCT FROM desired_revision AND desired_revision=(SELECT "
                "(revisions->>device_id::text)::bigint FROM bacuri_inventory.recorder_attempt WHERE site_id=$1)",
                {site, reason});
    } else {
        return;
    }
    db.exec("UPDATE bacuri_inventory.recorder_delivery SET decided_revision=$2 WHERE site_id=$1",
            {site, std::to_string(snapshot.revision())});
    db.exec("DELETE FROM bacuri_inventory.recorder_attempt WHERE site_id=$1", {site});
    db.exec("COMMIT");
}
} // namespace

void deliver(const std::stop_token& stop, const Key& key, const std::string& transport_policy)
{
    std::optional<std::string> policy{transport_policy};
    const Assignment assignment{setting("BACURI_SITE_ID"), setting("BACURI_NODE_ID")};
    auto recorder = v1::RecorderControl::NewStub(
        grpc::CreateChannel("unix:" + setting("BACURI_SOCKET"), grpc::experimental::LocalCredentials(UDS)));
    while (!stop.stop_requested()) {
        try {
            send_pending(*recorder, key, assignment, policy);
        } catch (const std::exception&) {
            std::fputs("Recorder delivery unavailable\n", stderr);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{500});
    }
}
} // namespace bacuri::vms::control
