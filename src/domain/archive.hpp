#pragma once

#include "domain/resources.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <variant>
#include <vector>

namespace bacuri::vms::domain {

struct BlockKey {
    SiteId site;
    BlockId id;
    RecordingEpochId recording_epoch;
    std::int64_t owner_generation;

    [[nodiscard]] friend constexpr bool operator==(const BlockKey&, const BlockKey&) noexcept = default;
};

enum class RecordingError {
    different_site,
    first_frame_not_at_begin,
};

class RecordedInterval {
public:
    [[nodiscard]] static std::expected<RecordedInterval, RecordingError> make(
        BlockKey block, BindingKey binding, UtcInterval interval, ProjectedTime first_frame
    ) noexcept;

    [[nodiscard]] const BlockKey& block() const noexcept { return block_; }
    [[nodiscard]] const BindingKey& binding() const noexcept { return binding_; }
    [[nodiscard]] const UtcInterval& interval() const noexcept { return interval_; }
    [[nodiscard]] const ProjectedTime& first_frame() const noexcept { return first_frame_; }

private:
    RecordedInterval(BlockKey block, BindingKey binding, UtcInterval interval, ProjectedTime first_frame) noexcept
        : block_{block}, binding_{binding}, interval_{interval}, first_frame_{first_frame} {}

    BlockKey block_;
    BindingKey binding_;
    UtcInterval interval_;
    ProjectedTime first_frame_;
};

enum class GapReason {
    source_offline,
    storage_failure,
    overload,
};

struct RecordingGap {
    BindingKey binding;
    UtcInterval interval;
    GapReason reason;
};

using ArchiveInterval = std::variant<RecordedInterval, RecordingGap>;

enum class TrackKind {
    video,
    audio,
};

struct MediaTrack {
    std::int32_t id;
    TrackKind kind;
    std::string codec;
    std::int32_t time_scale;
};

struct BlockManifestV1 {
    static constexpr std::int32_t version = 1;

    RecordedInterval recording;
    RecordingNodeId recording_node;
    DeviceId device;
    ChannelId channel;
    StreamProfileId profile;
    std::int64_t size_bytes;
    std::array<std::uint8_t, 32> sha256;
    std::vector<MediaTrack> tracks;
};

}  // namespace bacuri::vms::domain
