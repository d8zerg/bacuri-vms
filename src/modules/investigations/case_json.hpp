#pragma once
#include "application/control_api/http_json.hpp"
#include "modules/investigations/cases.hpp"
namespace bacuri::vms::control::investigations
{
inline void from_json(const Json& j, CaseBookmark& b)
{
    if (!j.is_object() || j.size() != 2)
        throw Failure{ErrorCode::invalid_request};
    b = {string_field(j, "at", 32), string_field(j, "name", 128)};
}
inline void to_json(Json& j, const CaseBookmark& b)
{
    j = {{"at", b.at}, {"name", b.name}};
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CaseInterval, start, end)
inline void from_json(const Json& j, CaseMaterialInput& input)
{
    if (!j.is_object() || j.size() != 3 || !j.contains("interval") || !j.contains("bookmarks") ||
        !j.at("bookmarks").is_array() || j.at("bookmarks").size() > 64)
        throw Failure{ErrorCode::invalid_request};
    const auto point = string_field(j, "point_id", 36);
    const auto& interval = j.at("interval");
    if (!canonical_id(point) || !interval.is_object() || interval.size() != 2)
        throw Failure{ErrorCode::invalid_request};
    input = {point,
             {string_field(interval, "start", 32), string_field(interval, "end", 32)},
             j.at("bookmarks").get<std::vector<CaseBookmark>>()};
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CaseMaterial, point_id, hold_id, interval, bookmarks)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ExportCase, id, number, assignee, notes, revision, materials)
} // namespace bacuri::vms::control::investigations
