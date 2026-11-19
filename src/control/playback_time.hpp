#pragma once
#include "control/api.hpp"
#include <google/protobuf/util/time_util.h>

namespace bacuri::vms::control
{
using ProtoTime = google::protobuf::util::TimeUtil;
using Timestamp = google::protobuf::Timestamp;
struct Window {
    std::int64_t start, end;
    Json json() const
    {
        return {{"start", ProtoTime::ToString(ProtoTime::MicrosecondsToTimestamp(start))},
                {"end", ProtoTime::ToString(ProtoTime::MicrosecondsToTimestamp(end))}};
    }
};
inline constexpr std::int64_t alert_context_us = 15'000'000;
inline Window alert_window(std::int64_t time)
{
    return {time - alert_context_us, time + alert_context_us};
}
inline std::int64_t utc_us(const Timestamp& t)
{
    if (t.seconds() < ProtoTime::kTimestampMinSeconds || t.seconds() > ProtoTime::kTimestampMaxSeconds ||
        t.nanos() < 0 || t.nanos() >= 1000000000 || t.nanos() % 1000 != 0) {
        throw Failure{400, "invalid_request"};
    }
    return ProtoTime::TimestampToMicroseconds(t);
}
inline std::int64_t parse_utc(const std::string& value)
{
    Timestamp t;
    if (!value.ends_with('Z') || !ProtoTime::FromString(value, &t)) {
        throw Failure{400, "invalid_request"};
    }
    return utc_us(t);
}
inline Window window(const Json& object)
{
    if (!object.is_object() || object.size() != 2) {
        throw Failure{400, "invalid_request"};
    }
    Window result{parse_utc(string_field(object, "start", 32)), parse_utc(string_field(object, "end", 32))};
    constexpr auto hour = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::hours{1}).count();
    if (result.end <= result.start || result.end - result.start > hour) {
        throw Failure{400, "invalid_request"};
    }
    return result;
}
} // namespace bacuri::vms::control
