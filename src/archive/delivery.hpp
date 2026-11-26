#pragma once
#include "archive/volume.hpp"
#include <stop_token>
namespace bacuri::vms::archive
{
void index_blocks(const std::stop_token&, Volume&, const std::filesystem::path&, const std::string& socket);
void deliver_blocks(const std::stop_token&, Volume&, const std::string& socket);

// Background integrity verification reads archived media at most at this rate.
inline constexpr std::uintmax_t verification_bytes_per_second = std::uintmax_t{32} * 1024 * 1024;
// Verifies due blocks until stopped and reports changed verdicts through the catalog delivery.
void verify_blocks(const std::stop_token&, Volume&);
} // namespace bacuri::vms::archive
