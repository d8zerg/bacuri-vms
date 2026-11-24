#pragma once
#include "control/configuration_snapshot.hpp"
namespace bacuri::vms::control::events
{
inline void rule_configuration(ConfigurationSnapshot& snapshot)
{
    snapshot.section("bacuri_events.video_rules");
}
} // namespace bacuri::vms::control::events
