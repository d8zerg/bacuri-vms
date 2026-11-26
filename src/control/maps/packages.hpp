#pragma once
#include "control/api.hpp"
#include <filesystem>
#include <stop_token>

namespace bacuri::vms::control::map_packages
{
namespace fs = std::filesystem;
constexpr std::uint64_t max_region = 8ULL << 30;
constexpr std::uint64_t max_archive = 32ULL << 30;
Json inspect(const fs::path& file);
void install(Database& db, const fs::path& file, const std::string& site, const Json& entry,
             std::string_view mode);
void manage(const Request&, Response&, Database&, const std::string& site, std::string_view path);
fs::path directory(const std::string& id);
void work(const std::stop_token& stop);
} // namespace bacuri::vms::control::map_packages
