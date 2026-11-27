#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
namespace bacuri::vms::archive
{
struct IndexAnchor {
    std::int64_t pts_us;
    std::string preview;
};
IndexAnchor index_anchor(const std::filesystem::path& file, const std::string& codec);
} // namespace bacuri::vms::archive
