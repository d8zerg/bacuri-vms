#include "archive/delivery.hpp"
#include "control.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <algorithm>
#include <condition_variable>

namespace bacuri::vms::archive
{
void deliver_blocks(std::stop_token stop, Volume& volume, const std::string& socket)
{
    using namespace std::chrono_literals;
    auto catalog = v1::ArchiveCatalog::NewStub(
        grpc::CreateChannel("unix:" + socket, grpc::experimental::LocalCredentials(UDS)));
    std::condition_variable_any timer;
    std::mutex mutex;
    std::unique_lock guard{mutex};
    auto retry = 1s;
    while (!stop.stop_requested()) {
        auto delay = 250ms;
        try {
            if (auto pending = volume.pending_gap()) {
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
                volume.acknowledge_gap(id);
            }
            if (auto item = volume.pending()) {
                grpc::ClientContext context;
                context.set_deadline(std::chrono::system_clock::now() + 3s);
                v1::RegisterBlockRequest request;
                request.set_manifest_json(item->manifest);
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
                                    result.state() == v1::REGISTRATION_STATE_REPLAYED)) {
                    volume.acknowledge(*item);
                    retry = 1s;
                    continue;
                }
                if (status.error_code() == grpc::StatusCode::ALREADY_EXISTS ||
                    status.error_code() == grpc::StatusCode::INVALID_ARGUMENT) {
                    volume.acknowledge(*item, "catalog_rejected");
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
