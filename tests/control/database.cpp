#include "control/database.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <thread>

namespace bacuri::vms::control
{
namespace
{
using namespace std::chrono_literals;

// These tests need the PostgreSQL stand. The control scenarios run them against an isolated database.
bool configured()
{
    return std::getenv("PGHOST") && std::getenv("PGDATABASE");
}

std::string backend(Database& db)
{
    return db.exec("SELECT pg_backend_pid()").get(0);
}

TEST(DatabasePool, ReleasingASessionHasTheEffectOfClosingIt)
{
    if (!configured())
        GTEST_SKIP() << "PostgreSQL is not configured";
    std::string first;
    {
        Database db;
        first = backend(db);
        db.exec("SELECT pg_advisory_lock(918273645)");
        db.exec("CREATE TEMP TABLE pool_probe(value integer)");
        db.exec("SET lock_timeout='1s'");
        db.exec("BEGIN");
        db.exec("SELECT pg_advisory_xact_lock(918273646)");
    }
    Database reused;
    ASSERT_EQ(backend(reused), first) << "An idle connection is reused";
    EXPECT_EQ(
        reused.exec("SELECT count(*) FROM pg_locks WHERE locktype='advisory' AND pid=pg_backend_pid()").get(0),
        "0");
    EXPECT_EQ(reused.exec("SELECT to_regclass('pg_temp.pool_probe') IS NULL").get(0), "t");
    EXPECT_EQ(reused.exec("SELECT current_setting('lock_timeout'),current_setting('statement_timeout')").get(0, 0),
              "4s");
    EXPECT_EQ(reused.exec("SELECT current_setting('statement_timeout')").get(0), "6s");
    EXPECT_EQ(reused.exec("SELECT current_setting('search_path')").get(0), "pg_catalog, pg_temp");
    EXPECT_EQ(reused.exec("SELECT now()=statement_timestamp()").get(0), "t") << "No transaction remains open";
}

TEST(DatabasePool, ConnectionClosedByTheServerIsReplaced)
{
    if (!configured())
        GTEST_SKIP() << "PostgreSQL is not configured";
    std::string closed;
    {
        Database observer;
        {
            Database idle;
            closed = backend(idle);
        }
        observer.exec("SELECT pg_terminate_backend($1)", {closed});
        for (int attempt = 0; attempt < 50; ++attempt) {
            if (observer.exec("SELECT count(*) FROM pg_stat_activity WHERE pid=$1", {closed}).get(0) == "0")
                break;
            std::this_thread::sleep_for(20ms);
        }
    }
    // The pool now holds the observer session and the closed one.
    Database held, replacement;
    EXPECT_NE(backend(held), closed);
    EXPECT_NE(backend(replacement), closed);
    EXPECT_EQ(replacement.exec("SELECT 1").get(0), "1");
}
} // namespace
} // namespace bacuri::vms::control
