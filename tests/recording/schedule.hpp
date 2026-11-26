#pragma once
#include <nlohmann/json.hpp>

namespace bacuri::vms::recording::test
{
inline nlohmann::json weekly()
{
    return {{"continuous", false},
            {"manual", true},
            {"event", true},
            {"audio", false},
            {"pre", 5},
            {"post", 10},
            {"days", nullptr},
            {"zone", "Europe/Berlin"},
            {"schedule", nlohmann::json::array({{{"day", 7}, {"start", 130}, {"end", 150}}})}};
}

} // namespace bacuri::vms::recording::test
