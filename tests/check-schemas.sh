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
sqlite3 -batch -bail "$sqlite_file" < storage/sqlite/migrations/0002_archive.sql
test "$(sqlite3 "$sqlite_file" 'PRAGMA user_version')" = 2
sqlite3 -batch -bail "$sqlite_file" < storage/sqlite/migrations/0003_coverage.sql
test "$(sqlite3 "$sqlite_file" 'PRAGMA user_version')" = 3
test "$(sqlite3 "$sqlite_file" 'SELECT registered_at_utc_us FROM committed_blocks WHERE unavailable_reason IS NULL')" = 1155
psql -Xq -v ON_ERROR_STOP=1 --dbname "$database" -f storage/postgresql/migrations/0002_control.sql
psql -Xq -v ON_ERROR_STOP=1 --dbname "$database" <<'SQL'
INSERT INTO bacuri_inventory.sites VALUES ('10000000-0000-4000-8000-000000000001','Existing site','UTC');
INSERT INTO bacuri_inventory.recorder_delivery(site_id,desired_revision,decided_revision)
VALUES ('10000000-0000-4000-8000-000000000001',4,4);
SQL
psql -Xq -v ON_ERROR_STOP=1 --dbname "$database" -f storage/postgresql/migrations/0003_archive.sql
test "$(psql -XAtq --dbname "$database" -c 'SELECT desired_revision-decided_revision FROM bacuri_inventory.recorder_delivery')" = 1
psql -Xq -v ON_ERROR_STOP=1 --dbname "$database" -f storage/postgresql/migrations/0004_playback.sql
printf '%s\n' 'PostgreSQL and SQLite schemas and archive upgrades passed'
