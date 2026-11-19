#pragma once
#include "control/support.hpp"
#include <stop_token>

namespace bacuri::vms::control
{
void deliver(std::stop_token stop, const Key& key);
}
