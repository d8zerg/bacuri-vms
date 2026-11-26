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
sqlite3 "$sqlite_file" "INSERT INTO pending_gaps VALUES(1,'point',1,1,2,'storage_failure')"
sqlite3 -batch -bail "$sqlite_file" < storage/sqlite/migrations/0004_gap_history.sql
test "$(sqlite3 "$sqlite_file" 'SELECT registered FROM archive_gaps WHERE id=1')" = 0
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
psql -Xq -v ON_ERROR_STOP=1 --dbname "$database" -f storage/postgresql/migrations/0005_live.sql
psql -Xq -v ON_ERROR_STOP=1 --dbname "$database" -f storage/postgresql/migrations/0006_alerts.sql
psql -Xq -v ON_ERROR_STOP=1 --dbname "$database" -f storage/postgresql/migrations/0007_recovery.sql
psql -Xq -v ON_ERROR_STOP=1 --dbname "$database" -f storage/postgresql/migrations/0008_camera_bindings.sql
psql -Xq -v ON_ERROR_STOP=1 --dbname "$database" -f storage/postgresql/migrations/0009_ptz_commands.sql
psql -Xq -v ON_ERROR_STOP=1 --dbname "$database" -f storage/postgresql/migrations/0010_maps.sql
psql -Xq -v ON_ERROR_STOP=1 --dbname "$database" <<'SQL'
DO $$
DECLARE
    site uuid := '10000000-0000-4000-8000-000000000001';
    point uuid := gen_random_uuid();
    device uuid := gen_random_uuid();
    channel uuid := gen_random_uuid();
    observation uuid := gen_random_uuid();
BEGIN
    INSERT INTO bacuri_inventory.observation_points VALUES (site,point,'Gate');
    INSERT INTO bacuri_inventory.devices(site_id,device_id,name) VALUES (site,device,'Camera');
    INSERT INTO bacuri_inventory.channels VALUES (site,channel,device,0);
    INSERT INTO bacuri_inventory.point_bindings(site_id,point_id,binding_revision,channel_id,valid_from_utc_us)
        VALUES (site,point,1,channel,0);
    INSERT INTO bacuri_events.observations(site_id,source_id,observation_id,type,schema_version,point_id,
        binding_revision,origin,observed_at_utc_us,received_at_utc_us,time_basis,attributes)
        VALUES (site,device,observation,'motion',1,point,1,'camera',1,1,'reception','{}');
    INSERT INTO bacuri_events.alerts(site_id,position,source_id,observation_id,point_id)
        VALUES (site,1,device,observation,point);
END $$;
SQL
psql -Xq -v ON_ERROR_STOP=1 --dbname "$database" -f storage/postgresql/migrations/0011_alert_work.sql
test "$(psql -XAtq --dbname "$database" -c "SELECT count(*) FROM bacuri_events.alerts WHERE
    work_state='new' AND work_version=0 AND owner_id IS NULL AND work_result=''")" = 1
sqlite3 -bail "$sqlite_file" < storage/sqlite/migrations/0005_retention.sql
psql -Xq -v ON_ERROR_STOP=1 --dbname "$database" -f storage/postgresql/migrations/0012_archive_retention.sql
psql -Xq -v ON_ERROR_STOP=1 --dbname "$database" -f storage/postgresql/migrations/0013_fragment_export.sql
psql -Xq -v ON_ERROR_STOP=1 --dbname "$database" -f storage/postgresql/migrations/0014_admin_mfa.sql
psql -Xq -v ON_ERROR_STOP=1 --dbname "$database" -f storage/postgresql/migrations/0015_credential_recovery.sql
set -- -f tests/schema/module-schemas.sql -f tests/schema/map-packages.sql
for migration in storage/postgresql/migrations/*.sql; do
    name=${migration##*/}
    if [ "$name" = 0031_investigation_owner.sql ]; then
        set -- "$@" -f tests/schema/investigation-owner.sql
    elif [ "${name%%_*}" -gt 17 ]; then
        set -- "$@" -f "$migration"
    fi
done
psql -Xq -v ON_ERROR_STOP=1 --dbname "$database" "$@" -f tests/schema/module-dependencies.sql
python3 -B tests/architecture/core_boundaries.py --database "$database"
sqlite3 -bail "$sqlite_file" < storage/sqlite/migrations/0006_recording_policies.sql
sqlite3 -bail "$sqlite_file" < storage/sqlite/migrations/0007_archive_holds.sql
test "$(sqlite3 "$sqlite_file" 'PRAGMA user_version')" = 7
test "$(sqlite3 "$sqlite_file" 'SELECT days,reserve_percent FROM retention')" = '0|20'
sqlite3 -bail "$sqlite_file" < storage/sqlite/migrations/0008_block_verification.sql
test "$(sqlite3 "$sqlite_file" 'PRAGMA user_version')" = 8
# Existing blocks are due for verification after the upgrade.
test "$(sqlite3 "$sqlite_file" 'SELECT count(*) FROM committed_blocks WHERE verified_generation=0 AND verified_at_utc_us IS NULL')" = \
    "$(sqlite3 "$sqlite_file" 'SELECT count(*) FROM committed_blocks')"
gaps_before="$(sqlite3 "$sqlite_file" 'SELECT count(*),coalesce(sum(registered),0) FROM archive_gaps')"
sqlite3 -bail "$sqlite_file" < storage/sqlite/migrations/0009_keyframe_gaps.sql
test "$(sqlite3 "$sqlite_file" 'PRAGMA user_version')" = 9
test "$(sqlite3 "$sqlite_file" 'SELECT count(*),coalesce(sum(registered),0) FROM archive_gaps')" = "$gaps_before"
sqlite3 -bail "$sqlite_file" "INSERT INTO archive_gaps(point,binding,begin_us,end_us,reason) VALUES('point',1,10,20,'keyframe_interval')"
if sqlite3 "$sqlite_file" "INSERT INTO archive_gaps(point,binding,begin_us,end_us,reason) VALUES('point',1,30,40,'unknown')" 2>/dev/null; then
    echo "Unknown gap reason accepted" >&2
    exit 1
fi
printf '%s\n' 'PostgreSQL and SQLite schemas and archive upgrades passed'
