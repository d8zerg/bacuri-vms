#pragma once
#include "control/api.hpp"
namespace bacuri::vms::control
{
class Sessions;
namespace recordings
{
void search_index(const Request&, Response&, const Sessions&, const std::string& site);
Json indexed_blocks(Database&, const std::string& site, const std::string& point, std::int64_t begin,
                    std::int64_t end, int limit);
} // namespace recordings
} // namespace bacuri::vms::control
