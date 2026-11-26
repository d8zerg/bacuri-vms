#pragma once
#include <array>
#include "application/control_api/api.hpp"
#include <filesystem>
#include <stop_token>

namespace bacuri::vms::control::map_packages
{
namespace fs = std::filesystem;
constexpr std::uint64_t max_region = 8ULL << 30;
constexpr std::uint64_t max_archive = 32ULL << 30;
struct RegionPackage {
    std::string id, name, source_time;
    std::array<double, 4> bounds{};
    std::uintmax_t bytes{};
};
RegionPackage inspect(const fs::path& file);
void install(postgresql::Database& db, const fs::path& file, const std::string& site, const RegionPackage& entry,
             std::string_view mode);
void manage(const Request&, Response&, postgresql::Database&, const std::string& site, std::string_view path);
fs::path directory(const std::string& id);
void work(const std::stop_token& stop);
} // namespace bacuri::vms::control::map_packages
