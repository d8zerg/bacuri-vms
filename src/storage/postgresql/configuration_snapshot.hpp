#pragma once
#include "storage/postgresql/database.hpp"
#include <nlohmann/json.hpp>
namespace bacuri::vms::postgresql
{
// Owners enumerate their sections in foreign-key order for both directions of the backup format.
class ConfigurationSnapshot
{
  public:
    explicit ConfigurationSnapshot(QuerySession& db) : db_{db} {}
    ConfigurationSnapshot(QuerySession& db, const nlohmann::json& input) : db_{db}, input_{&input} {}
    static bool requires_restore(nlohmann::json existing, const nlohmann::json& backup);
    static void require_single_site(const nlohmann::json& data, const std::string& site);
    void section(const char* table, const char* source = nullptr);
    nlohmann::json contents = nlohmann::json::object();

  private:
    QuerySession& db_;
    const nlohmann::json* input_{};
};
} // namespace bacuri::vms::postgresql
