#pragma once
#include "application/control_api/http_json.hpp"
#include "application/control_api/playback_time.hpp"
namespace bacuri::vms::control
{
inline void to_json(Json& j, const Window& span)
{
    j = {{"start", utc_text(span.start)}, {"end", utc_text(span.end)}};
}
inline Window window(const Json& object)
{
    if (!object.is_object() || object.size() != 2)
        throw Failure{ErrorCode::invalid_request};
    return checked_window(
        {parse_utc(string_field(object, "start", 32)), parse_utc(string_field(object, "end", 32))});
}
} // namespace bacuri::vms::control
