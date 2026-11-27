#pragma once
#include "media/acceleration/probe.hpp"
#include "media/acceleration/scheduler.hpp"

namespace bacuri::vms::acceleration
{
// Returns the verified session count for an encoder limit, or one for a verified workload.
unsigned verify_device(const HealthCheck& check, const Json& device, const Interrupt& interrupted,
                       const Run& run = subprocess);
} // namespace bacuri::vms::acceleration
