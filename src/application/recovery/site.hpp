#pragma once
#include <nlohmann/json.hpp>
namespace bacuri::vms::control
{
bool requires_restore(nlohmann::json existing, const nlohmann::json& backup);
inline void require_single_site(const nlohmann::json& data, const std::string& site)
{
    const auto& sites = data.at("sites");
    if (sites.size() != 1 || sites.at(0).at("site_id") != site) {
        throw std::runtime_error("Expected the assigned single-site inventory");
    }
}
} // namespace bacuri::vms::control
