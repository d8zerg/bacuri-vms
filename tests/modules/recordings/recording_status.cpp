#include "modules/recordings/status.hpp"

#include <gtest/gtest.h>

namespace bacuri::vms::control::recordings
{
namespace
{
using namespace std::chrono_literals;
const std::string point = "point";
constexpr std::chrono::microseconds lag = 15s;

RecorderSnapshot recorder(SourceReception source, PolicySelection selection,
                          StorageCapacity capacity = StorageCapacity::normal)
{
    return {.capacity = capacity,
            .points = {{point, {.source = source, .selection = selection, .publication_lag = lag}}}};
}

TEST(RecordingStatus, FreshArchiveWithReceivingSourceIsCurrent)
{
    const auto status = recording_status(lag + catalog_margin,
                                         recorder(SourceReception::receiving, PolicySelection::selected), point);
    EXPECT_EQ(status, (RecordingStatus{Connection::online, Recording::current, {}}));
}

TEST(RecordingStatus, ExpectedRecordingMissingFromArchiveIsDelayed)
{
    const auto receiving = recorder(SourceReception::receiving, PolicySelection::selected);
    const RecordingStatus delayed{Connection::online, Recording::delayed, RecordingReason::archive_pending};
    EXPECT_EQ(recording_status(lag + catalog_margin + 1us, receiving, point), delayed);
    EXPECT_EQ(recording_status(std::nullopt, receiving, point), delayed) << "No block reached the archive yet";
}

TEST(RecordingStatus, StoppedRecordingNamesTheCauseToFixFirst)
{
    const auto fresh = std::optional{1s};
    EXPECT_EQ(recording_status(fresh, recorder(SourceReception::offline, PolicySelection::selected), point),
              (RecordingStatus{Connection::offline, Recording::stopped, RecordingReason::source_offline}));
    EXPECT_EQ(recording_status(fresh,
                               recorder(SourceReception::receiving, PolicySelection::selected,
                                        StorageCapacity::recording_stopped),
                               point),
              (RecordingStatus{Connection::online, Recording::stopped, RecordingReason::storage_full}));
    EXPECT_EQ(recording_status(fresh, recorder(SourceReception::offline, PolicySelection::unbound), point),
              (RecordingStatus{Connection::offline, Recording::stopped, RecordingReason::unassigned}));
    EXPECT_EQ(recording_status(fresh,
                               recorder(SourceReception::offline, PolicySelection::waiting_event,
                                        StorageCapacity::recording_stopped),
                               point)
                  .reason,
              RecordingReason::storage_full);
}

TEST(RecordingStatus, PolicyThatDoesNotRecordNowStopsRecording)
{
    const std::pair<PolicySelection, RecordingReason> cases[] = {
        {PolicySelection::awaiting_policy, RecordingReason::no_policy},
        {PolicySelection::waiting_event, RecordingReason::waiting_event},
        {PolicySelection::outside_schedule, RecordingReason::outside_schedule},
        {PolicySelection::waiting_manual, RecordingReason::waiting_manual},
        {PolicySelection::disabled, RecordingReason::disabled}};
    for (const auto& [selection, reason] : cases) {
        const auto status = recording_status(1s, recorder(SourceReception::receiving, selection), point);
        EXPECT_EQ(status, (RecordingStatus{Connection::online, Recording::stopped, reason})) << name(reason);
    }
}

TEST(RecordingStatus, ConnectingSourceHasUnknownConnection)
{
    const auto status =
        recording_status(1s, recorder(SourceReception::connecting, PolicySelection::selected), point);
    EXPECT_EQ(status, (RecordingStatus{Connection::unknown, Recording::stopped, RecordingReason::source_offline}));
}

TEST(RecordingStatus, PointWithoutRecorderAssignmentIsUnassigned)
{
    const auto status =
        recording_status(1s, recorder(SourceReception::receiving, PolicySelection::selected), "other point");
    EXPECT_EQ(status, (RecordingStatus{Connection::unknown, Recording::stopped, RecordingReason::unassigned}));
}

TEST(RecordingStatus, UnavailableRecorderLeavesOnlyArchiveEvidence)
{
    EXPECT_EQ(recording_status(default_publication_lag + catalog_margin, std::nullopt, point),
              (RecordingStatus{Connection::unknown, Recording::current, {}}));
    EXPECT_EQ(recording_status(default_publication_lag + catalog_margin + 1us, std::nullopt, point),
              (RecordingStatus{Connection::unknown, Recording::unknown, RecordingReason::recorder_unavailable}));
    EXPECT_EQ(recording_status(std::nullopt, std::nullopt, point).recording, Recording::unknown);
}

TEST(RecordingStatus, EventBufferExtendsTheFreshnessWindow)
{
    auto snapshot = recorder(SourceReception::receiving, PolicySelection::selected);
    snapshot.points.at(point).publication_lag = 48s;
    EXPECT_EQ(recording_status(50s, snapshot, point).recording, Recording::current);
    EXPECT_EQ(recording_status(54s, snapshot, point).recording, Recording::delayed);
}
} // namespace
} // namespace bacuri::vms::control::recordings
