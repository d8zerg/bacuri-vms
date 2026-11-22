#include "control/database.hpp"

#include <array>
#include <span>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace bacuri::vms::control
{
std::string Rows::get(int row, int column) const
{
    if (row < 0 || row >= size() || column < 0 || column >= PQnfields(result_.get())) {
        throw std::runtime_error("Missing database value");
    }
    return PQgetvalue(result_.get(), row, column);
}

std::vector<std::string> Rows::column(int index) const
{
    std::vector<std::string> values;
    values.reserve(size());
    for (int row = 0; row < size(); ++row)
        values.push_back(get(row, index));
    return values;
}
std::string array_parameter(const std::vector<std::string>& elements)
{
    std::string value = "{";
    for (const auto& element : elements) {
        if (value.size() > 1)
            value += ',';
        value += '"';
        for (char c : element) {
            if (c == '"' || c == '\\')
                value += '\\';
            value += c;
        }
        value += '"';
    }
    return value + '}';
}

Database::Database() : connection_{PQconnectdb("connect_timeout=3"), PQfinish}
{
    if (!connection_ || PQstatus(connection_.get()) != CONNECTION_OK) {
        throw std::runtime_error("Database unavailable");
    }
    exec("SET search_path TO pg_catalog,pg_temp; SET statement_timeout='6s'; SET lock_timeout='4s'");
}

Rows Database::exec(const char* query, std::initializer_list<std::string> arguments)
{
    std::vector<const char*> values;
    for (const auto& argument : arguments) {
        if (argument.contains('\0')) {
            throw std::invalid_argument("Invalid text parameter");
        }
        values.push_back(argument.c_str());
    }
    auto* result = values.empty() ? PQexec(connection_.get(), query)
                                  : PQexecParams(connection_.get(), query, static_cast<int>(values.size()),
                                                 nullptr, values.data(), nullptr, nullptr, 0);
    if (!result) {
        throw std::runtime_error("Database operation failed");
    }
    const auto status = PQresultStatus(result);
    Rows rows{result};
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        throw std::runtime_error("Database operation failed");
    }
    return rows;
}

void migrate()
{
    Database db;
    db.exec("SELECT pg_advisory_lock(486324017)");
    int version = 0;
    if (db.exec("SELECT to_regclass('bacuri_meta.schema_migrations') IS NOT NULL").get(0) == "t") {
        version = std::stoi(db.exec(schema_version_query).get(0));
    } else if (db.exec("SELECT to_regclass('bacuri_inventory.schema_migrations') IS NOT NULL").get(0) == "t") {
        version = std::stoi(db.exec(legacy_schema_version_query).get(0));
    }
    constexpr std::array migrations{"0001_initial.sql",
                                    "0002_control.sql",
                                    "0003_archive.sql",
                                    "0004_playback.sql",
                                    "0005_live.sql",
                                    "0006_alerts.sql",
                                    "0007_recovery.sql",
                                    "0008_camera_bindings.sql",
                                    "0009_ptz_commands.sql",
                                    "0010_maps.sql",
                                    "0011_alert_work.sql",
                                    "0012_archive_retention.sql",
                                    "0013_fragment_export.sql",
                                    "0014_admin_mfa.sql",
                                    "0015_credential_recovery.sql",
                                    "0016_module_schemas.sql",
                                    "0017_map_packages.sql",
                                    "0018_camera_orientation.sql",
                                    "0019_plan_georeference.sql",
                                    "0020_query_indexes.sql",
                                    "0021_access_policy.sql"};
    if (version > static_cast<int>(migrations.size())) {
        throw std::runtime_error("Unsupported database schema");
    }

    for (const auto name : std::span{migrations}.subspan(version)) {
        std::ifstream file(std::filesystem::path{"storage/postgresql/migrations"} / name);
        if (!file) {
            throw std::runtime_error("Migration file unavailable");
        }
        std::string sql{std::istreambuf_iterator<char>{file}, {}};
        db.exec(sql.c_str());
    }
}
} // namespace bacuri::vms::control
