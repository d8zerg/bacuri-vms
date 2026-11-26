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
} // namespace bacuri::vms::postgresql
