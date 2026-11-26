#pragma once
#include "domain/image_region.hpp"
#include <nlohmann/json.hpp>
namespace bacuri::vms::domain
{
inline void from_json(const nlohmann::json& j, ImageRegion& r)
{
    r = {j.at("left"), j.at("top"), j.at("right"), j.at("bottom")};
    if (j.size() != 4 ||
        !(r.left >= 0 && r.top >= 0 && r.right <= 1 && r.bottom <= 1 && r.left < r.right && r.top < r.bottom))
        throw std::invalid_argument("Invalid image region");
}
inline void to_json(nlohmann::json& j, const ImageRegion& r)
{
    j = {{"left", r.left}, {"top", r.top}, {"right", r.right}, {"bottom", r.bottom}};
}
inline ImageRegions decode_regions(std::string_view text)
{
    if (text.empty())
        return {};
    const auto j = nlohmann::json::parse(text);
    if (!j.is_array() || j.size() > 32)
        throw std::invalid_argument("Invalid video mask");
    return j.get<ImageRegions>();
}
inline std::string encode_regions(const ImageRegions& regions)
{
    return regions.empty() ? std::string{} : nlohmann::json(regions).dump();
}
} // namespace bacuri::vms::domain
