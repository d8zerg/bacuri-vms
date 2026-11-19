#pragma once

#include "domain/resources.hpp"

#include <array>
#include <cstdint>
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

struct RecordedInterval {
    BlockKey block;
    BindingKey binding;
    UtcInterval interval;
    ProjectedTime first_frame;
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
