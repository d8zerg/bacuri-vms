#pragma once
#include "domain/image_region_json.hpp"
#include "archive/analysis.hpp"
#include <nlohmann/json.hpp>
namespace bacuri::vms::archive
{
inline void from_json(const nlohmann::json& j, MotionSample& sample)
{
    if (j.size() != 3 || !j.at("at").is_number_integer())
        throw std::invalid_argument("Invalid motion sample");
    sample = {j.at("at"), j.at("cells"), j.at("image")};
}
inline void to_json(nlohmann::json& j, const MotionSample& sample)
{
    j = {{"at", sample.offset}, {"cells", sample.cells}, {"image", sample.image}};
}
inline void from_json(const nlohmann::json& j, MotionIndex& index)
{
    if (j.size() != 2 || !j.at("frames").is_array() || j.at("frames").size() > 4096 ||
        !j.at("samples").is_array() || j.at("samples").size() > 256)
        throw std::invalid_argument("Invalid motion index");
    for (const auto& t : j.at("frames"))
        if (!t.is_number_integer())
            throw std::invalid_argument("Invalid indexed timestamp");
    j.at("frames").get_to(index.frames);
    j.at("samples").get_to(index.samples);
}
inline void to_json(nlohmann::json& j, const MotionIndex& index)
{
    j = {{"frames", index.frames}, {"samples", index.samples}};
}
} // namespace bacuri::vms::archive
