#pragma once
#include "application/control_api/playback_time.hpp"
#include "modules/investigations/cases.hpp"
namespace bacuri::vms::control::investigations
{
void validate_bookmarks(const std::vector<CaseBookmark>& bookmarks, Window span);
}
