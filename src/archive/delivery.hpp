#pragma once
#include "archive/volume.hpp"
#include <stop_token>
namespace bacuri::vms::archive
{
void index_blocks(std::stop_token, Volume&, const std::filesystem::path&, const std::string& socket);
void deliver_blocks(std::stop_token, Volume&, const std::string& socket);
} // namespace bacuri::vms::archive
