#pragma once
#include <nlohmann/json.hpp>
#include "archive/verify.hpp"
namespace bacuri::vms::archive
{
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TrustedExportKey, site_id, algorithm, public_key)
}
