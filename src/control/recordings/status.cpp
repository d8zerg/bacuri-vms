#include "control/recordings/status.hpp"

namespace bacuri::vms::control::recordings
{
namespace
{
std::optional<RecordingReason> policy_reason(PolicySelection selection)
{
    switch (selection) {
    case PolicySelection::selected:
        return {};
    case PolicySelection::awaiting_policy:
        return RecordingReason::no_policy;
    case PolicySelection::waiting_event:
        return RecordingReason::waiting_event;
    case PolicySelection::outside_schedule:
        return RecordingReason::outside_schedule;
    case PolicySelection::waiting_manual:
        return RecordingReason::waiting_manual;
    case PolicySelection::disabled:
        return RecordingReason::disabled;
    case PolicySelection::unbound:
        return RecordingReason::unassigned;
    }
    return RecordingReason::disabled;
}

Connection connection(SourceReception source)
{
    switch (source) {
    case SourceReception::receiving:
        return Connection::online;
    case SourceReception::offline:
        return Connection::offline;
    case SourceReception::connecting:
        break;
    }
    return Connection::unknown;
}
} // namespace

RecordingStatus recording_status(std::optional<std::chrono::microseconds> since_last_recorded,
                                 const std::optional<RecorderSnapshot>& recorder, const std::string& point)
{
    const auto fresh = [&](std::chrono::microseconds lag) {
        return since_last_recorded && *since_last_recorded <= lag + catalog_margin;
    };
    if (!recorder) {
        // Only the archive can still prove that recording is current.
        return fresh(default_publication_lag) ? RecordingStatus{Connection::unknown, Recording::current, {}}
                                              : RecordingStatus{Connection::unknown, Recording::unknown,
                                                                RecordingReason::recorder_unavailable};
    }
    const auto assigned = recorder->points.find(point);
    if (assigned == recorder->points.end())
        return {Connection::unknown, Recording::stopped, RecordingReason::unassigned};
    const auto& state = assigned->second;
    const auto link = connection(state.source);
    // The reasons are ordered by what an administrator has to fix first.
    if (state.selection == PolicySelection::unbound)
        return {link, Recording::stopped, RecordingReason::unassigned};
    if (recorder->capacity == StorageCapacity::recording_stopped)
        return {link, Recording::stopped, RecordingReason::storage_full};
    if (const auto reason = policy_reason(state.selection))
        return {link, Recording::stopped, reason};
    if (state.source != SourceReception::receiving)
        return {link, Recording::stopped, RecordingReason::source_offline};
    if (fresh(state.publication_lag))
        return {link, Recording::current, {}};
    return {link, Recording::delayed, RecordingReason::archive_pending};
}

std::string_view name(Connection value)
{
    switch (value) {
    case Connection::online:
        return "online";
    case Connection::offline:
        return "offline";
    case Connection::unknown:
        break;
    }
    return "unknown";
}

std::string_view name(Recording value)
{
    switch (value) {
    case Recording::current:
        return "current";
    case Recording::delayed:
        return "delayed";
    case Recording::stopped:
        return "stopped";
    case Recording::unknown:
        break;
    }
    return "unknown";
}

std::string_view name(RecordingReason value)
{
    switch (value) {
    case RecordingReason::source_offline:
        return "source_offline";
    case RecordingReason::storage_full:
        return "storage_full";
    case RecordingReason::no_policy:
        return "no_policy";
    case RecordingReason::waiting_event:
        return "waiting_event";
    case RecordingReason::outside_schedule:
        return "outside_schedule";
    case RecordingReason::waiting_manual:
        return "waiting_manual";
    case RecordingReason::disabled:
        return "disabled";
    case RecordingReason::unassigned:
        return "unassigned";
    case RecordingReason::archive_pending:
        return "archive_pending";
    case RecordingReason::recorder_unavailable:
        break;
    }
    return "recorder_unavailable";
}
} // namespace bacuri::vms::control::recordings
