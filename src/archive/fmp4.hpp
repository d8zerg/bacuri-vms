#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Fragmented MP4 (ISO/IEC 14496-12) with one H.264 or H.265 video track (ISO/IEC 14496-15).
namespace bacuri::vms::archive::fmp4
{
enum class Codec { h264, h265 };

// One access unit in decode order as an Annex B byte stream with start codes.
struct Sample {
    std::span<const std::byte> access_unit;
    // Presentation time relative to the start of the block.
    std::uint64_t pts_ns;
    std::uint64_t duration_ns;
    bool keyframe;
};

// Media time of the track in ticks per second.
inline constexpr std::uint32_t timescale = 90'000;
// A fragment starts at the first sample whose decode time is this far from the start of the previous fragment.
inline constexpr std::chrono::milliseconds fragment_duration{500};

// The file starts with ftyp and moov, continues with moof and mdat pairs and ends with an mfra index of the
// keyframes. The decoder configuration comes from the parameter sets of the first sample, which must be a
// keyframe. Parameter sets and access unit delimiters are removed from the samples. Throws
// std::invalid_argument when the samples cannot form such a file.
std::vector<std::byte> encode(Codec, std::span<const Sample>);
} // namespace bacuri::vms::archive::fmp4
