#include "archive/delivery.hpp"
#include "archive/analysis.hpp"
#include "archive/mp4.hpp"
#include "observability/metrics.hpp"
#include <fcntl.h>
#include <unistd.h>
#include "control.grpc.pb.h"
#include "platform/scope_exit.hpp"
#include <grpcpp/grpcpp.h>
#include <algorithm>
#include <condition_variable>

namespace bacuri::vms::archive
{
void index_blocks(const std::stop_token& stop, Volume& volume, const std::filesystem::path& root,
                  const std::string& socket)
{
    using namespace std::chrono_literals;
    auto catalog = v1::ArchiveCatalog::NewStub(
        grpc::CreateChannel("unix:" + socket, grpc::experimental::LocalCredentials(UDS)));
    std::mutex mutex;
    std::unique_lock guard{mutex};
    std::condition_variable_any timer;
    while (!stop.stop_requested()) {
        auto delay = 1000ms;
        try {
            if (volume.verification.scrub_copy())
                delay = 20ms;
        } catch (const std::exception&) {
            std::fputs("Archive copy volume unavailable\n", stderr);
        }
        try {
            v1::RetentionQuery query;
            query.set_site_id(volume.owner().site);
            query.set_recording_node_id(volume.owner().node);
            grpc::ClientContext ctx;
            ctx.set_deadline(std::chrono::system_clock::now() + 2s);
            v1::IndexJob job;
            if (catalog->NextIndex(&ctx, query, &job).ok() && !job.manifest().empty()) {
                const auto m = parse_manifest(Json::parse(job.manifest()));
                const auto input = root / "blocks" / m.recording.block().id.to_string() / "media.mp4";
                const int fd = open(input.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
                platform::ScopeExit close_input{[fd] {
                    if (fd >= 0)
                        close(fd);
                }};
                if (fd < 0 || archive::digest_hex(m.sha256) != digest_media(fd).sha256)
                    throw std::runtime_error("Archive integrity failure");
                auto index = analyze_recording(std::filesystem::path{"/proc/self/fd"} / std::to_string(fd),
                                               m.end_us() - m.begin_us());
                v1::IndexResult data;
                data.set_manifest(job.manifest());
                data.set_analysis(index.dump());
                grpc::ClientContext send;
                send.set_deadline(std::chrono::system_clock::now() + 3s);
                google::protobuf::Empty reply;
                if (catalog->CommitIndex(&send, data, &reply).ok())
                    delay = 50ms;
            }
        } catch (const std::exception&) {
            std::fputs("Archive motion indexing deferred\n", stderr);
        }
        timer.wait_for(guard, stop, delay, [] { return false; });
    }
}
void verify_blocks(const std::stop_token& stop, Volume& volume)
{
    using namespace std::chrono_literals;
    using Clock = std::chrono::steady_clock;
    // New blocks are verified when published, so an idle volume only waits for the next periodic check.
    constexpr auto idle = 30s, failure_backoff = 5s;
    std::condition_variable_any timer;
    std::mutex mutex;
    std::unique_lock guard{mutex};
    auto eligible = Clock::now();
    while (!stop.stop_requested()) {
        auto wake = Clock::now() + idle;
        try {
            if (const auto result = volume.verification.verify_next()) {
                auto& metrics = observability::Registry::process();
                metrics
                    .counter({.name = "bacuri_archive_verified_bytes_total",
                              .help = "Archive bytes read by integrity verification."})
                    .increment(static_cast<double>(result->bytes_read));
                metrics
                    .counter({.name = "bacuri_archive_verified_blocks_total",
                              .help = "Archive blocks verified by outcome."},
                             {{"outcome", result->available ? "available" : "unavailable"}})
                    .increment();
                eligible = std::max(eligible, Clock::now()) +
                           std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>{
                               static_cast<double>(result->bytes_read) / verification_bytes_per_second});
                wake = eligible;
            }
        } catch (const std::exception& error) {
            std::fprintf(stderr, "archive verification deferred: %s\n", error.what());
            wake = Clock::now() + failure_backoff;
        }
        timer.wait_until(guard, stop, wake, [] { return false; });
    }
}

void deliver_blocks(const std::stop_token& stop, Volume& volume, recording::RecordingPlan& plan,
                    const std::string& socket, const DetectorUpdate& update_detectors)
{
    using namespace std::chrono_literals;
    auto catalog = v1::ArchiveCatalog::NewStub(
        grpc::CreateChannel("unix:" + socket, grpc::experimental::LocalCredentials(UDS)));
    std::condition_variable_any timer;
    std::mutex mutex;
    std::unique_lock guard{mutex};
    auto retry = 1s;
    auto refresh = std::chrono::steady_clock::time_point::min();
    while (!stop.stop_requested()) {
        auto delay = 250ms;
        if (std::chrono::steady_clock::now() >= refresh) {
            try {
                grpc::ClientContext context;
                context.set_deadline(std::chrono::system_clock::now() + 3s);
                v1::RetentionQuery query;
                query.set_supports_holds(true);
                query.set_site_id(volume.owner().site);
                query.set_recording_node_id(volume.owner().node);
                v1::RetentionPolicy policy;
                if (catalog->AdvanceRetention(&context, query, &policy).ok()) {
                    update_detectors({policy.detector_points().begin(), policy.detector_points().end()});
                    plan.update(policy.has_recording_plan_json() ? policy.recording_plan_json() : "{}");
                    if (!policy.retirement_suspended()) {
                        volume.retention.protect_blocks(
                            {policy.protected_blocks().begin(), policy.protected_blocks().end()});
                        volume.retention.remember_cutoffs(
                            {policy.point_cutoffs().begin(), policy.point_cutoffs().end()});
                        volume.retention.apply_retention({.cutoff_us = policy.cutoff_us(),
                                                          .days = policy.days(),
                                                          .reserve_percent = policy.reserve_percent()});
                    }
                } else
                    volume.retention.reclaim_authorized();
            } catch (const std::exception& error) {
                std::fprintf(stderr, "archive retention pending: %s\n", error.what());
            }
            refresh = std::chrono::steady_clock::now() + 2s;
        }
        try {
            if (auto pending = volume.journal.pending_gap()) {
                const auto& [id, gap] = *pending;
                grpc::ClientContext context;
                context.set_deadline(std::chrono::system_clock::now() + 3s);
                v1::RegisterGapRequest q;
                q.mutable_point()->set_site_id(volume.owner().site);
                q.mutable_point()->set_point_id(gap.point);
                q.set_recording_node_id(volume.owner().node);
                q.set_binding_revision(gap.binding);
                q.mutable_interval()->set_begin_us(gap.begin);
                q.mutable_interval()->set_end_us(gap.end);
                q.set_reason(gap.reason);
                v1::RegistrationResult result;
                if (!catalog->RegisterGap(&context, q, &result).ok()) {
                    throw std::runtime_error("Gap catalog unavailable");
                }
                volume.journal.acknowledge_gap(id);
            }
            if (auto item = volume.journal.pending()) {
                grpc::ClientContext context;
                context.set_deadline(std::chrono::system_clock::now() + 3s);
                v1::RegisterBlockRequest request;
                request.set_manifest_json(manifest_json(item->manifest).dump());
                request.set_verification_generation(volume.generation());
                if (item->unavailable == "missing") {
                    request.set_availability(v1::BLOCK_AVAILABILITY_MISSING);
                }
                if (item->unavailable == "corrupt") {
                    request.set_availability(v1::BLOCK_AVAILABILITY_CORRUPT);
                }
                v1::RegistrationResult result;
                const auto status = catalog->RegisterBlock(&context, request, &result);
                if (status.ok() && (result.state() == v1::REGISTRATION_STATE_COMMITTED ||
                                    result.state() == v1::REGISTRATION_STATE_REPLAYED ||
                                    result.state() == v1::REGISTRATION_STATE_RETIRED)) {
                    volume.journal.acknowledge(*item);
                    retry = 1s;
                    continue;
                }
                if (status.error_code() == grpc::StatusCode::ALREADY_EXISTS ||
                    status.error_code() == grpc::StatusCode::INVALID_ARGUMENT) {
                    volume.journal.acknowledge(*item, "catalog_rejected");
                    std::fprintf(stderr, "archive catalog_rejected %s\n", item->directory.c_str());
                    continue;
                }
                throw std::runtime_error("Catalog unavailable");
            }
        } catch (const std::exception& error) {
            std::fprintf(stderr, "archive delivery pending: %s\n", error.what());
            delay = retry;
            retry = std::min(retry * 2, 30s);
        }
        timer.wait_for(guard, stop, delay, [] { return false; });
    }
}
} // namespace bacuri::vms::archive
