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
export BACURI_TEST_CAMERA_HOST="${BACURI_TEST_CAMERA_HOST:-rtsp-camera}"
fixture_ip=$(getent ahostsv4 "$BACURI_TEST_CAMERA_HOST" | awk 'NR == 1 { print $1 }')
test -n "$fixture_ip"
export BACURI_TEST_CAMERA_NETWORK="$fixture_ip/32"
export BACURI_CAMERA_NETWORKS="$BACURI_TEST_CAMERA_NETWORK,192.168.0.0/16"
export BACURI_TLS_CERT="$test_dir/server.crt" BACURI_TLS_KEY="$test_dir/server.key"
export BACURI_KEY_FILE="$test_dir/core.key" BACURI_SOCKET="$test_dir/recorder.sock"
export BACURI_RECORDER_DB="$test_dir/recorder.db"
export BACURI_CATALOG_SOCKET="$test_dir/catalog.sock" BACURI_ARCHIVE_DIR="$test_dir/archive"
export BACURI_AUTH_SOCKET="$test_dir/auth.sock" BACURI_MEDIA_SOCKET="$test_dir/media.sock"
export BACURI_PREVIEW_SOCKET="$test_dir/preview.sock"
mkdir -p "$BACURI_ARCHIVE_DIR/blocks"
export BACURI_LISTEN=127.0.0.1 BACURI_PORT=8443 BACURI_ORIGIN=https://localhost:8443
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc -days 1 -subj /CN=localhost \
    -addext 'subjectAltName=DNS:localhost' -keyout "$BACURI_TLS_KEY" -out "$BACURI_TLS_CERT" 2>/dev/null
openssl rand -hex 32 > "$BACURI_KEY_FILE"
openssl rand -hex 32 > "$test_dir/recorder.key"
psql -Xq -v ON_ERROR_STOP=1 -f storage/postgresql/migrations/0001_initial.sql
psql -Xq -v ON_ERROR_STOP=1 -c "INSERT INTO bacuri_inventory.sites VALUES ('$BACURI_SITE_ID','Existing site','UTC')"
"$BACURI_TEST_BIN/bacuri-vms-core" migrate
"$BACURI_TEST_BIN/bacuri-vms-core" migrate
case "${2:-control}" in
    control) python3 tests/control/registry.py "$test_dir" ;;
    archive) PYTHONDONTWRITEBYTECODE=1 python3 tests/archive/recording.py "$test_dir" ;;
    playback|live)
        export BACURI_LISTEN=0.0.0.0 BACURI_ORIGIN="https://$(hostname -i | awk '{print $1}'):8443"
        if [ "$2" = live ]; then
            PYTHONDONTWRITEBYTECODE=1 python3 tests/live/streaming.py "$test_dir"
        else
            PYTHONDONTWRITEBYTECODE=1 python3 tests/archive/playback.py "$test_dir"
        fi ;;
    *) exit 2 ;;
esac
