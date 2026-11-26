#pragma once
#include "domain/image_region.hpp"
#include <filesystem>
#include <string_view>
#include <cstdint>
namespace bacuri::vms::archive
{
struct ClipFrames {
    std::int64_t begin_us{}, end_us{}, count{};
};
ClipFrames select_frames(const std::filesystem::path& input, const std::filesystem::path& output,
                         std::int64_t from_us, std::int64_t until_us, bool fragmented = false,
                         const domain::ImageRegions& regions = {});
} // namespace bacuri::vms::archive
