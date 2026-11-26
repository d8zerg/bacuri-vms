#pragma once

#include <array>
#include <string>
#include <string_view>

namespace bacuri::vms::control::events
{
enum class WorkDecision { allowed, state_conflict, different_owner, missing_result };

struct WorkAction {
    std::string_view name, from, to;
};

inline constexpr WorkAction claim{"claim", "new", "in_progress"};
inline constexpr WorkAction record_result{"record_result", "in_progress", "in_progress"};
inline constexpr WorkAction close{"close", "in_progress", "closed"};
inline constexpr std::array work_actions{claim, record_result, close};

struct AlertWork {
    std::string state, owner;
    int version;
    bool has_result;

    WorkDecision evaluate(const WorkAction& action, std::string_view actor, int expected_version) const
    {
        if (expected_version != version || state != action.from)
            return WorkDecision::state_conflict;
        if (action.name != claim.name && actor != owner)
            return WorkDecision::different_owner;
        if (action.name == close.name && !has_result)
            return WorkDecision::missing_result;
        return WorkDecision::allowed;
    }
};
} // namespace bacuri::vms::control::events
