#pragma once
#include "application/control_api/support.hpp"
#include <stop_token>

namespace bacuri::vms::control
{
void deliver(const std::stop_token& stop, const Key& key, const std::string& transport_policy);
}
