#pragma once
#include "archive/verify.hpp"
#include "platform/secrets.hpp"

namespace bacuri::vms::archive
{
TrustedExportKey export_public_key(const platform::Key& seed, const std::string& site);
std::string sign_description(const platform::Key& seed, const std::string& description);
} // namespace bacuri::vms::archive
