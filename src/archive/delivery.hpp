#pragma once
#include "archive/volume.hpp"
#include <stop_token>
namespace bacuri::vms::archive
{
void deliver_blocks(std::stop_token, Volume&, const std::string& socket);
}
