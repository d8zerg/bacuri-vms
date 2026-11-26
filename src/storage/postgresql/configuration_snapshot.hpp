#pragma once
#include "storage/postgresql/database.hpp"
#include <nlohmann/json.hpp>
namespace bacuri::vms::postgresql
{
// Owners enumerate their sections in foreign-key order for both directions of the backup format.
class ConfigurationSnapshot
{
  public:
    explicit ConfigurationSnapshot(Database& db) : db_{db} {}
    ConfigurationSnapshot(Database& db, const nlohmann::json& input) : db_{db}, input_{&input} {}
    void section(const char* table, const char* source = nullptr);
    nlohmann::json contents = nlohmann::json::object();

  private:
    Database& db_;
    const nlohmann::json* input_{};
};
} // namespace bacuri::vms::postgresql
