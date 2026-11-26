#include "control/database.hpp"

#include <poll.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <span>
#include <stdexcept>

namespace bacuri::vms::control
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
            throw std::runtime_error("Database unavailable");
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
        throw std::runtime_error(std::string{"Database operation failed: "} + PQerrorMessage(connection_.get()));
    }
    const auto status = PQresultStatus(result);
    Rows rows{result};
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        // The state code and the primary message identify the cause in the service journal.
        const char* state = PQresultErrorField(result, PG_DIAG_SQLSTATE);
        const char* message = PQresultErrorField(result, PG_DIAG_MESSAGE_PRIMARY);
        throw std::runtime_error(std::string{"Database operation failed: "} + (state ? state : "unknown") + " " +
                                 (message ? message : PQresStatus(status)));
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
                                    "0021_access_policy.sql",
                                    "0022_camera_batches.sql",
                                    "0023_ptz_zoom.sql",
                                    "0024_camera_controls.sql",
                                    "0025_monitoring.sql",
                                    "0026_recording_policies.sql",
                                    "0027_archive_holds.sql",
                                    "0028_motion_index_queue.sql",
                                    "0029_keyframe_gaps.sql"};
    if (version > static_cast<int>(migrations.size())) {
        throw std::runtime_error("Unsupported database schema");
    }

    for (const auto name : std::span{migrations}.subspan(static_cast<std::size_t>(version))) {
        std::ifstream file(std::filesystem::path{"storage/postgresql/migrations"} / name);
        if (!file) {
            throw std::runtime_error("Migration file unavailable");
        }
        std::string sql{std::istreambuf_iterator<char>{file}, {}};
        db.exec(sql.c_str());
    }
}
} // namespace bacuri::vms::control
