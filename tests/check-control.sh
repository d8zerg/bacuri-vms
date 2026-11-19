#!/bin/sh
set -eu
umask 077
export BACURI_TEST_BIN="build/${1:-gcc-debug}"
test_dir=$(mktemp -d)
database=bacuri_control_$(basename "$test_dir" | tr -cd '[:alnum:]')
cleanup() {
    dropdb --if-exists -- "$database"
    rm -r -- "$test_dir"
}
createdb --template=template0 -- "$database"
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
export PGDATABASE="$database"
export BACURI_SITE_ID=10000000-0000-4000-8000-000000000001
export BACURI_NODE_ID=70000000-0000-4000-8000-000000000001
export BACURI_CAMERA_NETWORKS=10.0.0.0/8,192.168.0.0/16
export BACURI_TLS_CERT="$test_dir/server.crt" BACURI_TLS_KEY="$test_dir/server.key"
export BACURI_KEY_FILE="$test_dir/core.key" BACURI_SOCKET="$test_dir/recorder.sock"
export BACURI_RECORDER_DB="$test_dir/recorder.db"
export BACURI_LISTEN=127.0.0.1 BACURI_PORT=8443 BACURI_ORIGIN=https://localhost:8443
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc -days 1 -subj /CN=localhost \
    -addext 'subjectAltName=DNS:localhost' -keyout "$BACURI_TLS_KEY" -out "$BACURI_TLS_CERT" 2>/dev/null
openssl rand -hex 32 > "$BACURI_KEY_FILE"
openssl rand -hex 32 > "$test_dir/recorder.key"
psql -Xq -v ON_ERROR_STOP=1 -f storage/postgresql/migrations/0001_initial.sql
psql -Xq -v ON_ERROR_STOP=1 -c "INSERT INTO bacuri_inventory.sites VALUES ('$BACURI_SITE_ID','Existing site','UTC')"
"$BACURI_TEST_BIN/bacuri-vms-core" migrate
"$BACURI_TEST_BIN/bacuri-vms-core" migrate
python3 tests/control/registry.py "$test_dir"
