#include "modules/recordings/recorder.hpp"
#include "control.grpc.pb.h"
#include "media/lease.hpp"

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <cstdio>

namespace bacuri::vms::control::recordings
{
namespace
{
using namespace std::chrono_literals;
// The recorder answers from memory, so a slow answer means it is overloaded or stopped.
constexpr auto deadline = 300ms;

SourceReception reception(v1::SourceState state)
{
    switch (state) {
    case v1::SOURCE_STATE_RECEIVING:
        return SourceReception::receiving;
    case v1::SOURCE_STATE_CONNECTING:
        return SourceReception::connecting;
    default:
        return SourceReception::offline;
    }
}

PolicySelection selection(v1::RecordingSelection state)
{
    switch (state) {
    case v1::RECORDING_SELECTION_SELECTED:
        return PolicySelection::selected;
    case v1::RECORDING_SELECTION_AWAITING_POLICY:
        return PolicySelection::awaiting_policy;
    case v1::RECORDING_SELECTION_WAITING_EVENT:
        return PolicySelection::waiting_event;
    case v1::RECORDING_SELECTION_OUTSIDE_SCHEDULE:
        return PolicySelection::outside_schedule;
    case v1::RECORDING_SELECTION_WAITING_MANUAL:
        return PolicySelection::waiting_manual;
    case v1::RECORDING_SELECTION_UNBOUND:
        return PolicySelection::unbound;
    default:
        return PolicySelection::disabled;
    }
}

StorageCapacity capacity(v1::StorageCapacity state)
{
    switch (state) {
    case v1::STORAGE_CAPACITY_RECORDING_STOPPED:
        return StorageCapacity::recording_stopped;
    case v1::STORAGE_CAPACITY_LOW_SPACE:
        return StorageCapacity::low_space;
    default:
        return StorageCapacity::normal;
    }
}

// Clients poll point states, so only a change of recorder availability is logged.
void report_availability(bool available, const std::string& detail = {})
{
    static std::atomic_bool previous{true};
    if (previous.exchange(available) != available)
        std::fprintf(stderr, "recorder point states %s%s%s\n", available ? "available" : "unavailable",
                     detail.empty() ? "" : ": ", detail.c_str());
}
} // namespace

std::optional<RecorderSnapshot> recorder_snapshot(const std::string& site)
{
    try {
        auto recorder = v1::RecorderControl::NewStub(media::local_channel("BACURI_SOCKET"));
        grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + deadline);
        v1::PointStatesQuery query;
        query.set_site_id(site);
        v1::PointStates reply;
        if (const auto status = recorder->GetPointStates(&context, query, &reply); !status.ok()) {
            report_availability(false, status.error_message());
            return {};
        }
        report_availability(true);
        RecorderSnapshot snapshot{.capacity = capacity(reply.capacity()), .points = {}};
        for (const auto& point : reply.points())
            snapshot.points.insert_or_assign(
                point.point_id(),
                RecorderPoint{.source = reception(point.source()),
                              .selection = selection(point.selection()),
                              .publication_lag = std::chrono::microseconds{point.publication_lag_us()}});
        return snapshot;
    } catch (const std::exception& error) {
        report_availability(false, error.what());
        return {};
    }
}
} // namespace bacuri::vms::control::recordings
