#pragma once
#include "archive/manifest.hpp"
#include "control/support.hpp"

namespace bacuri::vms::archive
{
inline constexpr std::int64_t evidence_limit = 256LL * 1024 * 1024;
MediaDigest file_digest(const std::filesystem::path& path);
Json export_public_key(const control::Key& seed, const std::string& site);
std::string sign_description(const control::Key& seed, const std::string& description);
Json verify_evidence(const std::filesystem::path& directory, const Json& trusted_key);
} // namespace bacuri::vms::archive
