#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace bacuri::vms::control::recordings
{
// What the recorder reports about a point. The values mirror the recorder contract.
enum class SourceReception { receiving, connecting, offline };
enum class PolicySelection {
    selected,
    awaiting_policy,
    waiting_event,
    outside_schedule,
    waiting_manual,
    disabled,
    unbound
};
enum class StorageCapacity { normal, low_space, recording_stopped };

struct RecorderPoint {
    SourceReception source;
    PolicySelection selection;
    // Longest expected delay between a received frame and its published block.
    std::chrono::microseconds publication_lag;
};
struct RecorderSnapshot {
    StorageCapacity capacity;
    std::map<std::string, RecorderPoint> points;
};

// The newest available catalogued block of a point.
struct LastRecorded {
    // Archive position of the end of the block.
    std::int64_t end_utc_us;
    // Reception time of the last frame of the block on the recording node.
    std::int64_t received_until_us;
};

enum class Connection { unknown, online, offline };
enum class Recording { unknown, current, delayed, stopped };
enum class RecordingReason {
    source_offline,
    storage_full,
    no_policy,
    waiting_event,
    outside_schedule,
    waiting_manual,
    disabled,
    unassigned,
    archive_pending,
    recorder_unavailable
};

struct RecordingStatus {
    Connection connection{Connection::unknown};
    Recording recording{Recording::unknown};
    // Present unless the archive proves that recording is current.
    std::optional<RecordingReason> reason;

    friend bool operator==(const RecordingStatus&, const RecordingStatus&) = default;
};

// A catalogued block is registered within this margin after the recorder publishes it.
inline constexpr std::chrono::seconds catalog_margin{5};
// Publication lag assumed when the recorder does not report the point.
inline constexpr std::chrono::seconds default_publication_lag{15};

// Derives the recording state from the archive and the recorder report.
// `since_last_recorded` is the time since the last frame of the newest available block was received.
// `recorder` is empty when the recorder did not answer, and its point is absent when the point is not
// assigned to the recorder.
RecordingStatus recording_status(std::optional<std::chrono::microseconds> since_last_recorded,
                                 const std::optional<RecorderSnapshot>& recorder, const std::string& point);

std::string_view name(Connection);
std::string_view name(Recording);
std::string_view name(RecordingReason);
} // namespace bacuri::vms::control::recordings
