#pragma once
#include "application/control_api/api.hpp"
namespace bacuri::vms::control
{
class Sessions;
namespace recordings
{
void search_index(const Request&, Response&, const Sessions&, const std::string& site);
} // namespace recordings
} // namespace bacuri::vms::control
