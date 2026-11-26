#pragma once
#include "modules/media_sessions/operations.hpp"
#include "domain/image_region_json.hpp"
namespace bacuri::vms::control::media_sessions
{
inline void to_json(nlohmann::json& j, const ExportScope& s)
{
    j = {{"export_grant", s.grant},
         {"begin", s.begin},
         {"end", s.end},
         {"regions", domain::encode_regions(s.regions)},
         {"derivative", s.derivative},
         {"sound", s.sound}};
}
inline void from_json(const nlohmann::json& j, ExportScope& s)
{
    s = {j.at("export_grant"), j.at("begin"),
         j.at("end"),          domain::decode_regions(j.at("regions").get_ref<const std::string&>()),
         j.at("derivative"),   j.at("sound")};
}
} // namespace bacuri::vms::control::media_sessions
