#!/bin/sh
set -eu

schema_dir=$(mktemp -d)
database_suffix=$(basename "$schema_dir" | tr -cd '[:alnum:]')
database="bacuri_schema_$database_suffix"
sqlite_file="$schema_dir/journal.db"

cleanup() {
    result=$?
    trap - EXIT
    dropdb --if-exists -- "$database" || {
        if [ "$result" -eq 0 ]; then
            result=1
        fi
    }
    exit "$result"
}

python3 tests/schema/validate-manifest.py
createdb --template=template0 -- "$database"
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

psql -X --no-password --set ON_ERROR_STOP=1 --dbname "$database" \
    --set "manifest_fixture=$(cat tests/schema/block-manifest-v1.json)" \
    --set "insert_block_sql=$(cat storage/postgresql/insert-block.sql)" \
    --set "read_block_sql=$(cat storage/postgresql/read-block-manifest.sql)" \
    -f storage/postgresql/migrations/0001_initial.sql -f tests/schema/postgresql.sql

sqlite3 -batch -bail "$sqlite_file" < storage/sqlite/migrations/0001_initial.sql
sqlite3 -batch -bail "$sqlite_file" < tests/schema/sqlite.sql
test "$(sqlite3 -batch -bail "$sqlite_file" 'PRAGMA user_version;')" = 1
test "$(sqlite3 -batch -bail "$sqlite_file" 'PRAGMA journal_mode;')" = wal
test "$(sqlite3 -batch -bail "$sqlite_file" 'SELECT registered_at_utc_us FROM committed_blocks;')" = 1155
printf '%s\n' 'PostgreSQL and SQLite initial schemas passed'
