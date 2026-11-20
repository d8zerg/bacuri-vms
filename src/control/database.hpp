#pragma once

#include <libpq-fe.h>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace bacuri::vms::control
{

class Rows
{
  public:
    explicit Rows(PGresult* result) : result_{result, PQclear} {}
    int size() const
    {
        return PQntuples(result_.get());
    }
    std::string get(int row, int column = 0) const;

  private:
    std::unique_ptr<PGresult, decltype(&PQclear)> result_;
};

class Database
{
  public:
    Database();
    Rows exec(const char* query, std::initializer_list<std::string> arguments = {});

  private:
    std::unique_ptr<PGconn, decltype(&PQfinish)> connection_;
};

// Closing the connection rolls back a transaction that did not reach COMMIT.
void migrate();
} // namespace bacuri::vms::control
