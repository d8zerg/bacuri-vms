#include "platform/camera_networks.hpp"
#include "platform/rpc_status.hpp"
#include "platform/identifiers.hpp"
#include "platform/secrets.hpp"
#include "platform/settings.hpp"
#include "control.grpc.pb.h"
#include "recorder/sources.hpp"
#include "recorder/replay.hpp"
#include "archive/delivery.hpp"
#include "archive/writer.hpp"
#include "storage/sqlite.hpp"
#include "observability/metrics.hpp"
#include "platform/scope_exit.hpp"

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
#include <utility>

namespace bacuri::vms
{
namespace
{
using namespace platform;
using Snapshot = v1::ApplyConfigurationRequest;
using Decision = v1::ApplyConfigurationResult;

using storage::column;
// A recovery download request covers recordings between these moments in UTC microseconds. Earlier times mean
// an unset camera clock.
constexpr std::int64_t earliest_replay_us = 1'000'000'000'000;   // 2001-09-09
constexpr std::int64_t latest_replay_us = 4'102'444'800'000'000; // 2100-01-01
// One request copies at most one archive block of the camera.
constexpr std::chrono::microseconds max_replay_interval = archive::max_block_duration;
constexpr std::chrono::microseconds max_replay_uncertainty = std::chrono::seconds{5};
// Largest accepted difference between the camera clock and the site clock.
constexpr std::chrono::microseconds max_replay_offset = std::chrono::minutes{5};
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

    std::shared_ptr<ingest::FrameQueue> preview(const v1::PointKey& point, bool main)
    {
        std::scoped_lock lock{mutex_};
        return point.site_id() == site_
                   ? sources_.preview(point.point_id(), main ? ingest::Profile::main : ingest::Profile::secondary)
                   : nullptr;
    }

    grpc::Status RecoverGap(grpc::ServerContext* ctx, const v1::EdgeReplayRequest* q,
                            v1::EdgeReplayResult* out) override
    {
        if (q->point().site_id() != site_ || !canonical_id(q->point().point_id()) ||
            !canonical_id(q->device_id()) || q->recording_id().size() != 64 ||
            q->recording_id().find_first_not_of("0123456789abcdef") != std::string::npos ||
            q->begin_us() < earliest_replay_us || q->end_us() > latest_replay_us || q->end_us() <= q->begin_us() ||
            q->end_us() - q->begin_us() > max_replay_interval.count() || q->uncertainty_us() < 0 ||
            q->uncertainty_us() > max_replay_uncertainty.count() || q->offset_us() < -max_replay_offset.count() ||
            q->offset_us() > max_replay_offset.count())
            return platform::rpc_status(ErrorCode::invalid_request);
        std::unique_lock slot{replay_mutex_, std::try_to_lock};
        if (!slot.owns_lock())
            return platform::rpc_status(ErrorCode::replay_busy);
        try {
            archive::ReplayRequest job;
            {
                std::scoped_lock lock{mutex_};
                const auto it = std::ranges::find_if(active_.cameras(), [&](const auto& c) {
                    return c.point_id() == q->point().point_id() && c.device_id() == q->device_id() &&
                           c.binding_revision() == q->binding() && q->binding() > 0;
                });
                if (it == active_.cameras().end())
                    return platform::rpc_status(ErrorCode::binding_changed);
                auto pinned = networks_.stream_url(q->uri());
                if (!pinned)
                    return platform::rpc_status(ErrorCode::media_scope_denied);
                job = {{it->point_id(), it->device_id(), it->channel_id(), it->main_profile_id(), q->binding()},
                       q->recording_id(),
                       {*pinned, it->credentials().username(), it->credentials().password()},
                       q->begin_us(),
                       q->end_us(),
                       q->offset_us(),
                       q->uncertainty_us()};
            }
            const auto result = archive::recover_gap(volume_, job, [&] { return ctx->IsCancelled(); });
            out->set_blocks(result.blocks);
            out->set_duration_us(result.duration_us);
            out->set_outcome(result.outcome);
            out->set_covered_until_us(result.covered_until_us);
            return grpc::Status::OK;
        } catch (const std::exception&) {
            return platform::rpc_status(ErrorCode::dependency_unavailable);
        }
    }

    grpc::Status ExchangeAudio(grpc::ServerContext*, const v1::AudioRequest* q, v1::AudioChunk* out) override
    {
        if (q->site_id() != site_ || !canonical_id(q->point_id()))
            return platform::rpc_status(ErrorCode::invalid_request);
        try {
            if (q->mode() == v1::AudioRequest::ARCHIVE) {
                if (q->at_us() < 0 || q->at_us() > 256000 || !q->pcm().empty())
                    return platform::rpc_status(ErrorCode::invalid_request);
                out->set_pcm(volume_.read_audio(
                    {q->block_id(), q->sha256(), static_cast<std::size_t>(q->at_us()), q->length()}));
                return grpc::Status::OK;
            }
            std::shared_ptr<ingest::CameraAudio> audio;
            {
                std::scoped_lock lock{mutex_};
                audio = sources_.audio(q->point_id(), q->device_id());
            }
            if (!audio)
                return platform::rpc_status(ErrorCode::binding_changed);
            switch (q->mode()) {
            case v1::AudioRequest::LISTEN: {
                auto sound = audio->listen(q->at_us());
                out->set_start_us(sound.start_us);
                out->set_pcm(std::move(sound.pcm));
                break;
            }
            case v1::AudioRequest::RECORD:
            case v1::AudioRequest::RECORD_STOP:
                audio->record(q->operator_id(), q->mode() == v1::AudioRequest::RECORD);
                break;
            case v1::AudioRequest::TALK:
                audio->speak(q->talk_session_id(), std::chrono::milliseconds{q->talk_ttl_ms()}, q->pcm());
                break;
            case v1::AudioRequest::MUTE:
                audio->mute(q->talk_session_id());
                break;
            default:
                return platform::rpc_status(ErrorCode::invalid_request);
            }
            return grpc::Status::OK;
        } catch (const std::exception&) {
            return platform::rpc_status(ErrorCode::camera_audio_unavailable);
        }
    }

    grpc::Status GetVideoProfiles(grpc::ServerContext*, const v1::PointKey* point,
                                  v1::VideoProfiles* result) override
    {
        if (point->site_id() != site_ || !canonical_id(point->point_id()))
            return platform::rpc_status(ErrorCode::invalid_request);
        std::scoped_lock lock{mutex_};
        *result = sources_.video_profiles(point->point_id());
        return grpc::Status::OK;
    }
    grpc::Status GetCameraIssues(grpc::ServerContext*, const v1::CameraIssueQuery* query,
                                 v1::CameraIssues* result) override
    {
        if (query->site_id() != site_ || !canonical_id(query->device_id()))
            return platform::rpc_status(ErrorCode::invalid_request);
        std::scoped_lock lock{mutex_};
        *result = sources_.issues(query->device_id());
        return grpc::Status::OK;
    }

    grpc::Status GetStorageStatus(grpc::ServerContext*, const v1::RetentionQuery* q,
                                  v1::StorageStatus* out) override
    {
        if (q->site_id() != site_ || q->recording_node_id() != node_)
            return platform::rpc_status(ErrorCode::assignment_mismatch);
        try {
            auto status = volume_.status();
            std::scoped_lock guard{mutex_};
            status["point_states"] = sources_.recording_states();
            out->set_status_json(status.dump());
            return grpc::Status::OK;
        } catch (const std::exception&) {
            return platform::rpc_status(ErrorCode::dependency_unavailable);
        }
    }

    grpc::Status GetPointStates(grpc::ServerContext*, const v1::PointStatesQuery* q, v1::PointStates* out) override
    {
        if (q->site_id() != site_)
            return platform::rpc_status(ErrorCode::assignment_mismatch);
        try {
            {
                std::scoped_lock lock{mutex_};
                *out = sources_.point_states();
            }
            switch (volume_.capacity()) {
            case archive::CapacityState::normal:
                out->set_capacity(v1::STORAGE_CAPACITY_NORMAL);
                break;
            case archive::CapacityState::low_space:
                out->set_capacity(v1::STORAGE_CAPACITY_LOW_SPACE);
                break;
            case archive::CapacityState::recording_stopped:
                out->set_capacity(v1::STORAGE_CAPACITY_RECORDING_STOPPED);
                break;
            }
            return grpc::Status::OK;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "recorder point states unavailable: %s\n", error.what());
            return platform::rpc_status(ErrorCode::dependency_unavailable);
        }
    }

    grpc::Status ListPending(grpc::ServerContext*, const v1::PendingQuery* q,
                             v1::PendingIntervals* result) override
    {
        if (q->point().site_id() != site_ || !canonical_id(q->point().point_id()) ||
            q->begin_us() >= q->end_us() || (q->has_binding_revision() && q->binding_revision() < 1)) {
            return platform::rpc_status(ErrorCode::invalid_request);
        }
        try {
            for (const auto& [begin, end] : volume_.pending_intervals(
                     q->point().point_id(), q->begin_us(), q->end_us(),
                     q->has_binding_revision() ? std::optional{q->binding_revision()} : std::nullopt,
                     q->has_retention_cutoff_us() ? q->retention_cutoff_us()
                                                  : std::numeric_limits<std::int64_t>::min())) {
                auto* row = result->add_intervals();
                row->set_begin_us(begin);
                row->set_end_us(end);
            }
            return grpc::Status::OK;
        } catch (const std::exception&) {
            return platform::rpc_status(ErrorCode::dependency_unavailable);
        }
    }

    grpc::Status ApplyConfiguration(grpc::ServerContext*, const Snapshot* request, Decision* result) override
    {
        std::scoped_lock lock{mutex_};
        if (request->site_id() != site_ || request->recording_node_id() != node_) {
            return platform::rpc_status(ErrorCode::assignment_mismatch);
        }
        if (request->revision() <= 0) {
            return platform::rpc_status(ErrorCode::invalid_request);
        }
        try {
            return apply(*request, *result);
        } catch (const std::exception&) {
            return platform::rpc_status(ErrorCode::dependency_unavailable);
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
                return platform::rpc_status(ErrorCode::idempotency_conflict);
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
    std::mutex mutex_, replay_mutex_;
    archive::Volume volume_{setting("BACURI_ARCHIVE_DIR"),
                            {site_, node_},
                            {.replica = optional_setting("BACURI_ARCHIVE_REPLICA_DIR"), .invalid = {}}};
    std::jthread registration_{[this, socket = setting("BACURI_CATALOG_SOCKET")](const std::stop_token& stop) {
        archive::deliver_blocks(stop, volume_, socket);
    }};
    std::jthread indexer_{[this, socket = setting("BACURI_CATALOG_SOCKET")](const std::stop_token& stop) {
        archive::index_blocks(stop, volume_, setting("BACURI_ARCHIVE_DIR"), socket);
    }};
    std::jthread verifier_{[this](const std::stop_token& stop) { archive::verify_blocks(stop, volume_); }};
    ingest::Sources sources_{networks_, volume_};
};

class PreviewSource final : public v1::PreviewSource::Service
{
  public:
    explicit PreviewSource(Recorder& recorder) : recorder_{recorder} {}
    grpc::Status Subscribe(grpc::ServerContext* context, const v1::PointKey* point,
                           grpc::ServerWriter<v1::PreviewFrame>* stream) override
    {
        return stream_profile(context, *point, false, stream);
    }
    grpc::Status SubscribeProfile(grpc::ServerContext* context, const v1::PreviewRequest* request,
                                  grpc::ServerWriter<v1::PreviewFrame>* stream) override
    {
        return stream_profile(context, request->point(), request->main(), stream);
    }

  private:
    grpc::Status stream_profile(grpc::ServerContext* context, const v1::PointKey& point, bool main,
                                grpc::ServerWriter<v1::PreviewFrame>* stream)
    {
        using namespace std::chrono_literals;
        if (!slots_.try_acquire()) {
            return platform::rpc_status(ErrorCode::video_session_limit);
        }
        platform::ScopeExit slot{[this] { slots_.release(); }};
        auto queue = recorder_.preview(point, main);
        if (!queue) {
            return platform::rpc_status(ErrorCode::not_found);
        }
        auto last = std::chrono::steady_clock::now();
        while (!context->IsCancelled()) {
            if (auto frame = queue->wait_pop(std::min(last + 5s, std::chrono::steady_clock::now() + 100ms))) {
                v1::PreviewFrame packet;
                packet.set_access_unit(frame->access_unit.data(), frame->access_unit.size());
                packet.set_keyframe(frame->keyframe);
                packet.set_codec(frame->codec == ingest::VideoCodec::h265 ? "h265" : "h264");
                if (!stream->Write(packet)) {
                    break;
                }
                last = std::chrono::steady_clock::now();
            } else if (std::chrono::steady_clock::now() >= last + 5s) {
                return platform::rpc_status(ErrorCode::video_source_unavailable);
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
    if (argc == 2 && std::string_view{argv[1]} == "receive-replay")
        return bacuri::vms::ingest::replay_worker();
    if (argc == 2 && std::string_view{argv[1]} == "archive-status") {
        const auto status = bacuri::vms::archive::archive_status(
            bacuri::vms::platform::setting("BACURI_ARCHIVE_DIR"),
            bacuri::vms::platform::optional_setting("BACURI_ARCHIVE_REPLICA_DIR"));
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
    const auto metrics =
        bacuri::vms::observability::export_process_metrics(std::getenv("BACURI_METRICS_DIRECTORY"), "recorder");
    boost::asio::io_context lifecycle;
    boost::asio::signal_set shutdown{lifecycle, SIGINT, SIGTERM};
    bacuri::vms::Recorder recorder;
    grpc::ServerBuilder builder;
    builder.AddListeningPort("unix:" + bacuri::vms::platform::setting("BACURI_SOCKET"),
                             grpc::experimental::LocalServerCredentials(UDS));
    builder.RegisterService(&recorder);
    auto server = builder.BuildAndStart();
    if (!server) {
        throw std::runtime_error("Cannot start recorder socket");
    }
    bacuri::vms::PreviewSource preview{recorder};
    grpc::ServerBuilder preview_builder;
    const auto preview_path = bacuri::vms::platform::setting("BACURI_PREVIEW_SOCKET");
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
