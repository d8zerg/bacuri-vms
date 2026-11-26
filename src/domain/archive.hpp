#pragma once

#include "domain/resources.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace bacuri::vms::domain
{

// Archive and export sound: mono signed 16-bit little-endian PCM at 8000 Hz.
struct ArchiveSound {
    static constexpr std::int64_t sample_rate = 8000;
    static constexpr std::int64_t sample_period_us = 1'000'000 / sample_rate;
    static constexpr std::int64_t bytes_per_sample = 2;
    // Largest sound transfer between processes: one second.
    static constexpr std::int64_t max_chunk_bytes = sample_rate * bytes_per_sample;
};

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

class RecordedInterval
{
  public:
    [[nodiscard]] static std::expected<RecordedInterval, RecordingError>
    make(BlockKey block, BindingKey binding, UtcInterval interval, ProjectedTime first_frame) noexcept;

    [[nodiscard]] const BlockKey& block() const noexcept
    {
        return block_;
    }
    [[nodiscard]] const BindingKey& binding() const noexcept
    {
        return binding_;
    }
    [[nodiscard]] const UtcInterval& interval() const noexcept
    {
        return interval_;
    }
    [[nodiscard]] const ProjectedTime& first_frame() const noexcept
    {
        return first_frame_;
    }

  private:
    RecordedInterval(BlockKey block, BindingKey binding, UtcInterval interval, ProjectedTime first_frame) noexcept
        : block_{block}, binding_{binding}, interval_{interval}, first_frame_{first_frame}
    {
    }

    BlockKey block_;
    BindingKey binding_;
    UtcInterval interval_;
    ProjectedTime first_frame_;
};

enum class GapReason {
    source_offline,
    storage_failure,
    overload,
    // The camera sent no keyframe within the block limits and the following frames were discarded.
    keyframe_interval,
};
std::string_view name(GapReason) noexcept;
std::optional<GapReason> parse_gap_reason(std::string_view) noexcept;

// Identifiers have no default value, so the type is built only with all fields.
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
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

struct ManifestAudio {
    std::int64_t start_us, size_bytes;
    std::array<std::uint8_t, 32> sha256;
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
    std::optional<ManifestAudio> audio;
    std::optional<std::string> edge_recording;

    [[nodiscard]] std::int64_t begin_us() const noexcept
    {
        return recording.interval().begin().time_since_epoch().count();
    }
    [[nodiscard]] std::int64_t end_us() const noexcept
    {
        return recording.interval().end().time_since_epoch().count();
    }
};

} // namespace bacuri::vms::domain
