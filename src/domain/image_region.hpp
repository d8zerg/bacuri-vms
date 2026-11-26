#pragma once
#include <vector>
namespace bacuri::vms::domain
{
struct ImageRegion {
    double left{}, top{}, right{}, bottom{};
    bool operator==(const ImageRegion&) const = default;
    bool overlaps(double x, double y, double width, double height) const
    {
        return x < right && x + width > left && y < bottom && y + height > top;
    }
};
using ImageRegions = std::vector<ImageRegion>;
} // namespace bacuri::vms::domain
