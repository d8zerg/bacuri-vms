#pragma once
#include "archive/manifest.hpp"
#include <filesystem>
#include <optional>

namespace bacuri::vms::archive
{
// The mount exists before the recorder starts. Each site and writer owns a separate directory.
std::optional<std::filesystem::path> replica_blocks(const std::filesystem::path& primary, const Owner&);
bool verified_copy(const std::filesystem::path& directory, const Json& expected);
void restore_copy(const std::filesystem::path& source, const Json& expected, const std::filesystem::path& target);
void remove_copy(const std::filesystem::path& directory);
} // namespace bacuri::vms::archive
