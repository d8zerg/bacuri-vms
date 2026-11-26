#pragma once
#include "archive/manifest.hpp"
#include <filesystem>

// Verification of signed export and case packages. It depends only on libsodium and JSON, so an independent
// verifier can be built without the archive volume.
namespace bacuri::vms::archive
{
inline constexpr std::int64_t evidence_limit = 256LL * 1024 * 1024;
MediaDigest file_digest(const std::filesystem::path& path);
// Checks the signature with the trusted site key, then every file, period and part of the package.
// Throws std::invalid_argument for a package that does not verify.
struct TrustedExportKey {
    std::string site_id, algorithm, public_key;
};
struct VerifiedFragment {
    std::string point_id;
    std::int64_t begin_us{}, end_us{};
};
struct VerifiedEvidence {
    std::string signed_manifest, mode;
    std::optional<VerifiedFragment> fragment;
};
VerifiedEvidence verify_evidence(const std::filesystem::path& directory, const TrustedExportKey& trusted_key);
} // namespace bacuri::vms::archive
