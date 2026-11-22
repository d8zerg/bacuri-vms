#pragma once
#include "control/support.hpp"
#include <stop_token>

namespace bacuri::vms::control
{
void camera_events(std::stop_token, const CameraNetworks&, const Key&);

}
