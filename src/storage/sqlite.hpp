#pragma once
#include <sqlite3.h>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace bacuri::vms::storage
{
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

enum class OpenMode { read_write, read_only };

class Sqlite
{
  public:
    explicit Sqlite(const std::filesystem::path& path, OpenMode mode = OpenMode::read_write)
    {
        sqlite3* handle{};
        const int flags =
            mode == OpenMode::read_only ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
        auto code = sqlite3_open_v2(path.c_str(), &handle, flags, nullptr);
        db_.reset(handle);
        if (code != SQLITE_OK) {
            throw std::runtime_error("Cannot open SQLite database");
        }
        sqlite3_busy_timeout(handle, 3000);
        if (mode == OpenMode::read_write) {
            exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL");
        }
    }
    void exec(const char* sql)
    {
        if (sqlite3_exec(db_.get(), sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(db_.get()));
        }
    }
    Statement query(const char* sql)
    {
        sqlite3_stmt* stmt{};
        if (sqlite3_prepare_v2(db_.get(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(db_.get()));
        }
        return {stmt, sqlite3_finalize};
    }

  private:
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db_{nullptr, sqlite3_close};
};

inline bool next(sqlite3_stmt* stmt)
{
    auto code = sqlite3_step(stmt);
    if (code == SQLITE_ROW) {
        return true;
    }
    if (code == SQLITE_DONE) {
        return false;
    }
    throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(stmt)));
}
inline std::string column(sqlite3_stmt* stmt, int index)
{
    auto* value = sqlite3_column_text(stmt, index);
    if (!value) {
        throw std::runtime_error("Missing SQLite value");
    }
    return reinterpret_cast<const char*>(value);
}
inline void bind_text(sqlite3_stmt* stmt, int index, const std::string& value)
{
    if (sqlite3_bind_text(stmt, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) !=
        SQLITE_OK) {
        throw std::runtime_error("Cannot bind SQLite text");
    }
}
} // namespace bacuri::vms::storage
