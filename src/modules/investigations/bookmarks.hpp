#pragma once
#include "application/control_api/playback_time.hpp"
namespace bacuri::vms::control::investigations
{
void validate_bookmarks(const Json& bookmarks, Window span);
}
