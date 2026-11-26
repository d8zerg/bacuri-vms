#include "storage/postgresql/database.hpp"

#include <poll.h>

#include <mutex>
#include <stdexcept>
#include <string_view>

namespace bacuri::vms::postgresql
{
namespace
{
#define BACURI_SESSION_SETTINGS                                                                                   \
    "SET search_path TO pg_catalog,pg_temp; SET statement_timeout='6s'; SET lock_timeout='4s'"
constexpr char session_settings[] = BACURI_SESSION_SETTINGS;
// Ends a logical session on a reused connection with the effect of closing it.
constexpr char session_reset[] =
    "SELECT pg_advisory_unlock_all(); UNLISTEN *; DISCARD TEMP; RESET ALL; " BACURI_SESSION_SETTINGS;
#undef BACURI_SESSION_SETTINGS
// Idle connections kept for reuse. Concurrent sessions beyond this number open their own connections.
constexpr std::size_t max_idle_connections = 16;

bool succeeds(PGconn* connection, const char* commands)
{
    std::unique_ptr<PGresult, decltype(&PQclear)> result{PQexec(connection, commands), PQclear};
    const auto status = result ? PQresultStatus(result.get()) : PGRES_FATAL_ERROR;
    return status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK;
}

// Sessions do not listen for notifications, so the server writes to an idle connection only when it
// terminates the session or reports changed parameters. Such a connection is replaced.
bool alive(PGconn* connection)
{
    if (PQstatus(connection) != CONNECTION_OK || PQtransactionStatus(connection) != PQTRANS_IDLE)
        return false;
    pollfd socket{.fd = PQsocket(connection), .events = POLLIN, .revents = 0};
    return poll(&socket, 1, 0) == 0;
}

bool reset(PGconn* connection)
{
    if (PQstatus(connection) != CONNECTION_OK)
        return false;
    switch (PQtransactionStatus(connection)) {
    case PQTRANS_IDLE:
        break;
    case PQTRANS_INTRANS:
    case PQTRANS_INERROR:
        if (!succeeds(connection, "ROLLBACK"))
            return false;
        break;
    default:
        return false;
    }
    return succeeds(connection, session_reset) && PQtransactionStatus(connection) == PQTRANS_IDLE;
}

class Pool
{
  public:
    Pool()
    {
        idle_.reserve(max_idle_connections);
    }
    Database::Connection acquire()
    {
        for (;;) {
            Database::Connection idle{nullptr, PQfinish};
            {
                std::scoped_lock guard{mutex_};
                if (idle_.empty())
                    break;
                idle = std::move(idle_.back());
                idle_.pop_back();
            }
            if (alive(idle.get()))
                return idle;
        }
        Database::Connection opened{PQconnectdb("connect_timeout=3"), PQfinish};
        if (!opened || PQstatus(opened.get()) != CONNECTION_OK || !succeeds(opened.get(), session_settings))
            throw DatabaseUnavailable("Database unavailable");
        return opened;
    }
    // A connection that cannot be reset or kept is closed. The reserved storage keeps release free of
    // allocation, so it can run in a destructor.
    void release(Database::Connection connection) noexcept
    {
        if (!connection || !reset(connection.get()))
            return;
        std::scoped_lock guard{mutex_};
        if (idle_.size() < max_idle_connections)
            idle_.push_back(std::move(connection));
    }

  private:
    std::mutex mutex_;
    std::vector<Database::Connection> idle_;
};

Pool& pool()
{
    static Pool instance;
    return instance;
}
} // namespace
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
    values.reserve(static_cast<std::size_t>(size()));
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

Database::Database() : connection_{pool().acquire()} {}

Database::~Database()
{
    pool().release(std::move(connection_));
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
        if (PQstatus(connection_.get()) == CONNECTION_BAD)
            throw DatabaseUnavailable(PQerrorMessage(connection_.get()));
        throw std::runtime_error("Database returned no result");
    }
    const auto status = PQresultStatus(result);
    Rows rows{result};
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        // The state code and the primary message identify the cause in the service journal.
        const char* state = PQresultErrorField(result, PG_DIAG_SQLSTATE);
        const char* message = PQresultErrorField(result, PG_DIAG_MESSAGE_PRIMARY);
        const std::string_view sqlstate = state ? state : "unknown";
        const auto diagnostic = "Database operation failed: " + std::string{sqlstate} + " " +
                                (message ? message : PQresStatus(status));
        if (PQstatus(connection_.get()) == CONNECTION_BAD || sqlstate.starts_with("08") || sqlstate == "57P01" ||
            sqlstate == "57P02" || sqlstate == "57P03")
            throw DatabaseUnavailable(diagnostic);
        throw std::runtime_error(diagnostic);
    }
    return rows;
}
} // namespace bacuri::vms::postgresql
