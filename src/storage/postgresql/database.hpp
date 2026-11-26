#pragma once

#include <libpq-fe.h>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace bacuri::vms::postgresql
{

class DatabaseUnavailable : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

class Rows
{
  public:
    explicit Rows(PGresult* result) : result_{result, PQclear} {}
    int size() const
    {
        return PQntuples(result_.get());
    }
    std::string get(int row, int column = 0) const;
    std::vector<std::string> column(int index = 0) const;

  private:
    std::unique_ptr<PGresult, decltype(&PQclear)> result_;
};

std::string array_parameter(const std::vector<std::string>& elements);

class QuerySession
{
  public:
    virtual ~QuerySession() = default;
    virtual Rows exec(const char* query, std::initializer_list<std::string> arguments = {}) = 0;
};

// One logical database session. Connections are reused through a process-wide pool: releasing a
// session rolls back a transaction that did not reach COMMIT and resets session state, including
// session advisory locks and listeners, as closing the connection would.
class Database final : public QuerySession
{
  public:
    Database();
    ~Database() override;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Rows exec(const char* query, std::initializer_list<std::string> arguments = {}) override;

    using Connection = std::unique_ptr<PGconn, decltype(&PQfinish)>;

  private:
    Connection connection_;
};
} // namespace bacuri::vms::postgresql
