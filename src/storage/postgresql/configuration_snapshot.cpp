#include "storage/postgresql/configuration_snapshot.hpp"

namespace bacuri::vms::postgresql
{
void ConfigurationSnapshot::section(const char* table, const char* source)
{
    const std::string relation{table};
    const std::string key = relation.substr(relation.find('.') + 1);
    if (input_) {
        const auto query = "INSERT INTO " + relation +
                           " SELECT * FROM jsonb_populate_recordset(NULL::" + relation + ",$1::jsonb)";
        db_.exec(query.c_str(), {input_->at(key).dump()});
    } else {
        const auto query = std::string{"SELECT coalesce(jsonb_agg(to_jsonb(t) ORDER BY to_jsonb(t)),'[]') FROM "} +
                           (source ? source : table) + " t";
        contents[key] = nlohmann::json::parse(db_.exec(query.c_str()).get(0));
    }
}
bool ConfigurationSnapshot::requires_restore(nlohmann::json existing, const nlohmann::json& backup)
{
    if (std::ranges::all_of(existing, [](const auto& rows) { return rows.empty(); }))
        return true;
    // Replaying a backup preserves audit entries and policy revisions recorded since the backup.
    for (const auto* field : {"credential_audit", "access_audit", "policy_versions"})
        existing[field] = backup.at(field);
    if (existing != backup)
        throw std::runtime_error("Restore requires a clean database or an identical inventory");
    return false;
}

void ConfigurationSnapshot::require_single_site(const nlohmann::json& data, const std::string& site)
{
    const auto& sites = data.at("sites");
    if (sites.size() != 1 || sites.at(0).at("site_id") != site) {
        throw std::runtime_error("Expected the assigned single-site inventory");
    }
}
} // namespace bacuri::vms::postgresql
