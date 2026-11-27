#pragma once
#include "domain/image_region.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <span>

namespace bacuri::vms::archive
{
constexpr int preview_width = 64, preview_height = 48, grid_width = 16, grid_height = 12;
struct MotionSample {
    std::int64_t offset{};
    std::string cells, image;
    std::string backend{"cpu"};
};
struct MotionIndex {
    std::vector<std::int64_t> frames;
    std::vector<MotionSample> samples;
};
MotionSample motion_sample(std::span<const unsigned char> previous, std::span<const unsigned char> image,
                           std::int64_t offset);
MotionIndex analyze_recording(const std::filesystem::path&, std::int64_t duration,
                              const std::string& codec = "h264");
void validate_analysis(const MotionIndex&, std::int64_t duration);
} // namespace bacuri::vms::archive

namespace bacuri::vms::archive
{
using MotionArea = domain::ImageRegion;
struct Movement {
    int cells{};
    double x{}, y{};
};
Movement measure_motion(const std::string& cells, int threshold, const std::vector<MotionArea>& include,
                        const std::vector<MotionArea>& exclude);
std::string mask_preview(const std::string& image, const std::vector<MotionArea>& exclude);
} // namespace bacuri::vms::archive
