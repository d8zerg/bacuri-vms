#include "control/support.hpp"
#include "control.grpc.pb.h"
#include "recorder/sources.hpp"
#include "archive/delivery.hpp"
#include "storage/sqlite.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <grpcpp/grpcpp.h>
#include <sodium.h>
#include <sqlite3.h>
#include <sys/stat.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>

namespace bacuri::vms
{
namespace
{
using namespace control;
using Snapshot = v1::ApplyConfigurationRequest;
using Decision = v1::ApplyConfigurationResult;

using storage::Sqlite;
using storage::column;
using storage::next;

class Recorder final : public v1::RecorderControl::Service
{
public:
    Recorder()
        : site_{setting("BACURI_SITE_ID")}, node_{setting("BACURI_NODE_ID")},
          networks_{setting("BACURI_CAMERA_NETWORKS")}, key_{load_key(setting("BACURI_KEY_FILE"))},
          journal_{setting("BACURI_RECORDER_DB")}
    {
        journal_.exec("CREATE TABLE IF NOT EXISTS decisions(revision INTEGER PRIMARY KEY,digest TEXT NOT NULL,"
                      "state INTEGER NOT NULL,failure INTEGER NOT NULL,applied INTEGER NOT NULL,sealed TEXT)");
        auto row = journal_.query("SELECT revision,sealed FROM decisions WHERE sealed IS NOT NULL");
        if (next(row.get())) {
            auto rev = sqlite3_column_int64(row.get(), 0);
            if (!active_.ParseFromString(unseal(column(row.get(), 1), key_, context(rev))) ||
                active_.site_id() != site_ || active_.recording_node_id() != node_ || active_.revision() != rev) {
                throw std::runtime_error("Invalid persisted recorder assignment");
            }
        }
        sources_.apply(active_);
        std::fprintf(stderr, "Recorder active revision %lld, cameras %d\n",
                     static_cast<long long>(active_.revision()), active_.cameras_size());
    }

    grpc::Status ApplyConfiguration(grpc::ServerContext*, const Snapshot* request, Decision* result) override
    {
        std::scoped_lock lock{mutex_};
        if (request->site_id() != site_ || request->recording_node_id() != node_) {
            return {grpc::StatusCode::PERMISSION_DENIED, "Different recording assignment"};
        }
        if (request->revision() <= 0) {
            return {grpc::StatusCode::INVALID_ARGUMENT, "Invalid revision"};
        }
        try {
            return apply(*request, *result);
        } catch (const std::exception&) {
            return {grpc::StatusCode::UNAVAILABLE, "Recorder configuration unavailable"};
        }
    }

private:
    std::string context(std::int64_t revision) const
    {
        return site_ + "/" + node_ + "/" + std::to_string(revision);
    }

    bool valid(const Snapshot& request) const
    {
        std::set<std::string> devices;
        std::set<std::string> points;
        for (const auto& camera : request.cameras()) {
            const bool legacy = camera.binding_revision() == 0 && camera.channel_id().empty() &&
                                camera.main_profile_id().empty();
            const bool bound = camera.binding_revision() > 0 && canonical_id(camera.channel_id()) &&
                               canonical_id(camera.main_profile_id());
            if ((!legacy && !bound) || !canonical_id(camera.device_id()) || !canonical_id(camera.point_id()) ||
                !devices.insert(camera.device_id()).second || !points.insert(camera.point_id()).second ||
                !networks_.permits(camera.main_url()) || !networks_.permits(camera.secondary_url())) {
                return false;
            }
        }
        return true;
    }

    grpc::Status apply(const Snapshot& request, Decision& result)
    {
        const auto data = request.SerializeAsString();
        const auto digest = fingerprint(key_, "snapshot", data);
        result.set_requested_revision(request.revision());
        auto prior = journal_.query("SELECT digest,state,failure,applied FROM decisions WHERE revision=?");
        sqlite3_bind_int64(prior.get(), 1, request.revision());
        if (next(prior.get())) {
            if (column(prior.get(), 0) != digest) {
                return {grpc::StatusCode::ALREADY_EXISTS, "Revision content differs"};
            }
            result.set_state(static_cast<v1::ApplyState>(sqlite3_column_int(prior.get(), 1)));
            if (result.state() == v1::APPLY_STATE_REJECTED) {
                result.set_failure(static_cast<v1::ApplyFailure>(sqlite3_column_int(prior.get(), 2)));
            }
            if (result.state() == v1::APPLY_STATE_APPLIED && request.revision() == active_.revision()) {
                sources_.apply(active_);
            }
            const auto applied = sqlite3_column_int64(prior.get(), 3);
            if (applied > 0) {
                result.set_applied_revision(applied);
            }
            return grpc::Status::OK;
        }
        auto high = journal_.query("SELECT coalesce(max(revision),0) FROM decisions");
        next(high.get());
        if (request.revision() < sqlite3_column_int64(high.get(), 0)) {
            result.set_state(v1::APPLY_STATE_REJECTED);
            result.set_failure(v1::APPLY_FAILURE_STALE_REVISION);
            if (active_.revision() > 0) {
                result.set_applied_revision(active_.revision());
            }
            return grpc::Status::OK;
        }
        high.reset();
        const bool accepted = valid(request);
        result.set_state(accepted ? v1::APPLY_STATE_APPLIED : v1::APPLY_STATE_REJECTED);
        if (!accepted) {
            result.set_failure(v1::APPLY_FAILURE_INVALID_CONFIGURATION);
        }
        const auto applied = accepted ? request.revision() : active_.revision();
        if (applied > 0) {
            result.set_applied_revision(applied);
        }
        auto encrypted = accepted ? seal(key_, context(applied), data) : std::string{};
        Snapshot replacement;
        if (accepted) {
            replacement = request;
        }
        journal_.exec("BEGIN IMMEDIATE");
        try {
            if (accepted) {
                journal_.exec("UPDATE decisions SET sealed=NULL WHERE sealed IS NOT NULL");
            }
            auto insert = journal_.query("INSERT INTO decisions VALUES (?,?,?,?,?,?)");
            sqlite3_bind_int64(insert.get(), 1, request.revision());
            sqlite3_bind_text(insert.get(), 2, digest.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(insert.get(), 3, result.state());
            sqlite3_bind_int(insert.get(), 4, result.failure());
            sqlite3_bind_int64(insert.get(), 5, applied);
            if (accepted) {
                sqlite3_bind_text(insert.get(), 6, encrypted.c_str(), -1, SQLITE_TRANSIENT);
            }
            next(insert.get());
            journal_.exec("COMMIT");
        } catch (...) {
            journal_.exec("ROLLBACK");
            throw;
        }
        if (accepted) {
            active_.Swap(&replacement);
            sources_.apply(active_);
        }
        return grpc::Status::OK;
    }

    std::string site_;
    std::string node_;
    CameraNetworks networks_;
    Key key_;
    Sqlite journal_;
    Snapshot active_;
    std::mutex mutex_;
    archive::Volume volume_{setting("BACURI_ARCHIVE_DIR"), {site_, node_}};
    std::jthread registration_{[this, socket = setting("BACURI_CATALOG_SOCKET")](std::stop_token stop) {
        archive::deliver_blocks(stop, volume_, socket);
    }};
    ingest::Sources sources_{networks_, volume_};
};
} // namespace
} // namespace bacuri::vms

int main(int argc, char** argv)
try {
    if (argc == 2 && std::string_view{argv[1]} == "archive-status") {
        const auto status = bacuri::vms::archive::archive_status(bacuri::vms::control::setting("BACURI_ARCHIVE_DIR"));
        std::puts(status.dump().c_str());
        return 0;
    }
    if (argc != 1) {
        throw std::invalid_argument("Usage: bacuri-vms-recorder [archive-status]");
    }
    if (sodium_init() < 0) {
        return 1;
    }
    umask(0007);
    boost::asio::io_context lifecycle;
    boost::asio::signal_set shutdown{lifecycle, SIGINT, SIGTERM};
    bacuri::vms::Recorder recorder;
    grpc::ServerBuilder builder;
    builder.AddListeningPort("unix:" + bacuri::vms::control::setting("BACURI_SOCKET"),
                             grpc::experimental::LocalServerCredentials(UDS));
    builder.RegisterService(&recorder);
    auto server = builder.BuildAndStart();
    if (!server) {
        throw std::runtime_error("Cannot start recorder socket");
    }
    shutdown.async_wait([&server](const boost::system::error_code& error, int) {
        if (!error) {
            server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds{2});
        }
    });
    lifecycle.run();
    server->Wait();
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
