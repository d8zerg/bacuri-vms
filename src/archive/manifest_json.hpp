#pragma once
#include "archive/manifest.hpp"
#include <nlohmann/json.hpp>
namespace bacuri::vms::archive
{
using Json = nlohmann::json;
BlockManifest parse_manifest(const Json&);
Json manifest_json(const BlockManifest&);
} // namespace bacuri::vms::archive
