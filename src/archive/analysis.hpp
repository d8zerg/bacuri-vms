#pragma once
#include "archive/manifest.hpp"
#include <span>

namespace bacuri::vms::archive
{
constexpr int preview_width = 64, preview_height = 48, grid_width = 16, grid_height = 12;
Json motion_sample(std::span<const unsigned char> previous, std::span<const unsigned char> image,
                   std::int64_t offset);
Json analyze_recording(const std::filesystem::path&, std::int64_t duration);
void validate_analysis(const Json&, std::int64_t duration);
} // namespace bacuri::vms::archive

namespace bacuri::vms::archive
{
struct MotionArea {
    double left, top, right, bottom;
    static MotionArea parse(const Json&);
    bool overlaps(double x, double y, double width, double height) const;
};
struct Movement {
    int cells{};
    double x{}, y{};
};
Movement measure_motion(const std::string& cells, int threshold, const std::vector<MotionArea>& include,
                        const std::vector<MotionArea>& exclude);
std::string mask_preview(const std::string& image, const std::vector<MotionArea>& exclude);
} // namespace bacuri::vms::archive
