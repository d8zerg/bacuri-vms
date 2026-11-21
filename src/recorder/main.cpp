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

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <memory>
#include <mutex>
#include <set>
#include <semaphore>
#include <stdexcept>

namespace bacuri::vms
{
namespace
{
using namespace control;
using Snapshot = v1::ApplyConfigurationRequest;
using Decision = v1::ApplyConfigurationResult;

using storage::column;
using storage::next;
using storage::Sqlite;

class Recorder final : public v1::RecorderControl::Service
{
  public:
    Recorder()
        : site_{setting("BACURI_SITE_ID")}, node_{setting("BACURI_NODE_ID")},
          networks_{CameraNetworks::configured()}, key_{load_key(setting("BACURI_KEY_FILE"))},
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

    std::shared_ptr<ingest::FrameQueue> preview(const v1::PointKey& point)
    {
        std::scoped_lock lock{mutex_};
        return point.site_id() == site_ ? sources_.preview(point.point_id()) : nullptr;
    }

    grpc::Status GetCameraIssues(grpc::ServerContext*, const v1::CameraIssueQuery* query,
                                 v1::CameraIssues* result) override
    {
        if (query->site_id() != site_ || !canonical_id(query->device_id()))
            return {grpc::StatusCode::INVALID_ARGUMENT, "Invalid camera scope"};
        std::scoped_lock lock{mutex_};
        *result = sources_.issues(query->device_id());
        return grpc::Status::OK;
    }

    grpc::Status ListPending(grpc::ServerContext*, const v1::PendingQuery* q,
                             v1::PendingIntervals* result) override
    {
        if (q->point().site_id() != site_ || !canonical_id(q->point().point_id()) ||
            q->begin_us() >= q->end_us() || (q->has_binding_revision() && q->binding_revision() < 1)) {
            return {grpc::StatusCode::INVALID_ARGUMENT, "Invalid archive selection"};
        }
        try {
            for (const auto& [begin, end] : volume_.pending_intervals(
                     q->point().point_id(), q->begin_us(), q->end_us(),
                     q->has_binding_revision() ? std::optional{q->binding_revision()} : std::nullopt)) {
                auto* row = result->add_intervals();
                row->set_begin_us(begin);
                row->set_end_us(end);
            }
            return grpc::Status::OK;
        } catch (const std::exception&) {
            return {grpc::StatusCode::UNAVAILABLE, "Archive journal unavailable"};
        }
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
            const bool legacy =
                camera.binding_revision() == 0 && camera.channel_id().empty() && camera.main_profile_id().empty();
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

class PreviewSource final : public v1::PreviewSource::Service
{
  public:
    explicit PreviewSource(Recorder& recorder) : recorder_{recorder} {}
    grpc::Status Subscribe(grpc::ServerContext* context, const v1::PointKey* point,
                           grpc::ServerWriter<v1::PreviewFrame>* stream) override
    {
        using namespace std::chrono_literals;
        if (!slots_.try_acquire()) {
            return {grpc::StatusCode::RESOURCE_EXHAUSTED, "Preview capacity reached"};
        }
        struct Slot {
            std::counting_semaphore<16>& slots;
            ~Slot()
            {
                slots.release();
            }
        } slot{slots_};
        auto queue = recorder_.preview(*point);
        if (!queue) {
            return {grpc::StatusCode::NOT_FOUND, "Preview unavailable"};
        }
        auto last = std::chrono::steady_clock::now();
        while (!context->IsCancelled()) {
            if (auto frame = queue->wait_pop(std::min(last + 5s, std::chrono::steady_clock::now() + 100ms))) {
                v1::PreviewFrame packet;
                packet.set_access_unit(frame->h264.data(), frame->h264.size());
                packet.set_keyframe(frame->keyframe);
                if (!stream->Write(packet)) {
                    break;
                }
                last = std::chrono::steady_clock::now();
            } else if (std::chrono::steady_clock::now() >= last + 5s) {
                return {grpc::StatusCode::UNAVAILABLE, "Preview interrupted"};
            }
        }
        return grpc::Status::OK;
    }

  private:
    Recorder& recorder_;
    std::counting_semaphore<16> slots_{16};
};
} // namespace
} // namespace bacuri::vms

int main(int argc, char** argv)
try {
    if (argc == 2 && std::string_view{argv[1]} == "archive-status") {
        const auto status =
            bacuri::vms::archive::archive_status(bacuri::vms::control::setting("BACURI_ARCHIVE_DIR"));
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
    bacuri::vms::PreviewSource preview{recorder};
    grpc::ServerBuilder preview_builder;
    const auto preview_path = bacuri::vms::control::setting("BACURI_PREVIEW_SOCKET");
    preview_builder.AddListeningPort("unix:" + preview_path, grpc::experimental::LocalServerCredentials(UDS));
    preview_builder.RegisterService(&preview);
    auto preview_server = preview_builder.BuildAndStart();
    if (!preview_server || chmod(preview_path.c_str(), 0660) != 0) {
        throw std::runtime_error("Cannot open preview socket");
    }
    shutdown.async_wait([&](const boost::system::error_code& error, int) {
        if (!error) {
            auto deadline = std::chrono::system_clock::now() + std::chrono::seconds{2};
            preview_server->Shutdown(deadline);
            server->Shutdown(deadline);
        }
    });
    lifecycle.run();
    server->Wait();
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
