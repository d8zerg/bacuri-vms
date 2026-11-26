#pragma once
#include "archive/manifest.hpp"
#include <filesystem>
#include <optional>

namespace bacuri::vms::archive
{
// The directory of this writer on the configured copy mount, or nothing without a copy. The mount exists
// before the recorder starts. Each site and writer owns a separate directory.
std::optional<std::filesystem::path> replica_blocks(const std::optional<std::filesystem::path>& mount,
                                                    const std::filesystem::path& primary, const Owner&);
bool verified_copy(const std::filesystem::path& directory, const BlockManifest& expected);
void restore_copy(const std::filesystem::path& source, const BlockManifest& expected,
                  const std::filesystem::path& target);
void remove_copy(const std::filesystem::path& directory);
} // namespace bacuri::vms::archive
