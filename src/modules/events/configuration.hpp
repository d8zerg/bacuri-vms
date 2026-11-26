#pragma once
#include "storage/postgresql/configuration_snapshot.hpp"
namespace bacuri::vms::control::events
{
inline void rule_configuration(postgresql::ConfigurationSnapshot& snapshot)
{
    snapshot.section("bacuri_events.video_rules");
}
} // namespace bacuri::vms::control::events
