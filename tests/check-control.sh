#!/bin/sh
set -eu
umask 077
export BACURI_TEST_BIN="build/${1:-gcc-debug}"
export BACURI_OBSERVATION_SOURCE=80000000-0000-4000-8000-000000000001
test_dir=$(mktemp -d)
database=bacuri_control_$(basename "$test_dir" | tr -cd '[:alnum:]')
if [ "${1:-}" = clang-asan-ubsan ]; then
    export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}log_path=$test_dir/asan"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}log_path=$test_dir/ubsan"
fi
cleanup() {
    result=$?
    # A daemon can emit a sanitizer diagnostic during shutdown after the last assertion.
    for diagnostic in "$test_dir"/asan.* "$test_dir"/ubsan.*; do
        if [ -s "$diagnostic" ]; then
            cat "$diagnostic" >&2
            result=1
        fi
    done
    dropdb --if-exists -- "$database" || result=1
    rm -r -- "$test_dir" || result=1
    exit "$result"
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
export BACURI_LEGACY_RTSP_ADDRESSES="$fixture_ip,192.168.1.3"
export BACURI_TEST_CAMERA_NETWORK="$fixture_ip/32"
export BACURI_CAMERA_NETWORKS="$BACURI_TEST_CAMERA_NETWORK,192.168.0.0/16"
export BACURI_TLS_CERT="$test_dir/server.crt" BACURI_TLS_KEY="$test_dir/server.key"
export BACURI_KEY_FILE="$test_dir/core.key" BACURI_SOCKET="$test_dir/recorder.sock"
export BACURI_RECORDER_DB="$test_dir/recorder.db" BACURI_OBSERVATION_SOCKET="$test_dir/events.sock"
export BACURI_CATALOG_SOCKET="$test_dir/catalog.sock" BACURI_ARCHIVE_DIR="$test_dir/archive"
export BACURI_AUTH_SOCKET="$test_dir/auth.sock" BACURI_MEDIA_SOCKET="$test_dir/media.sock"
export BACURI_PREVIEW_SOCKET="$test_dir/preview.sock"
# Each service writes its Prometheus exposition to core.prom, recorder.prom or media.prom here.
export BACURI_METRICS_DIRECTORY="$test_dir"
if [ "${2:-}" = recovery ] || [ "${2:-}" = retention ] || [ "${2:-}" = hold-capacity ]; then
    fault_dir=/var/lib/bacuri-vms/fault-test
    test "$(stat -f -c %T "$fault_dir")" = tmpfs
    export BACURI_ARCHIVE_DIR="$fault_dir/archive"
fi
mkdir -p "$BACURI_ARCHIVE_DIR/blocks"
export BACURI_LISTEN=127.0.0.1 BACURI_PORT=8443 BACURI_ORIGIN=https://localhost:8443
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc -days 1 -subj /CN=localhost \
    -addext 'subjectAltName=DNS:localhost' -keyout "$BACURI_TLS_KEY" -out "$BACURI_TLS_CERT" 2>/dev/null
export BACURI_MAP_DIRECTORY="$test_dir/map-packages"
export BACURI_EXPORT_KEY_FILE="$test_dir/export.key"
openssl rand -hex 32 > "$BACURI_EXPORT_KEY_FILE"
openssl rand -hex 32 > "$BACURI_KEY_FILE"
openssl rand -hex 32 > "$test_dir/recorder.key"
psql -Xq -v ON_ERROR_STOP=1 -f storage/postgresql/migrations/0001_initial.sql
psql -Xq -v ON_ERROR_STOP=1 -c "INSERT INTO bacuri_inventory.sites VALUES ('$BACURI_SITE_ID','Existing site','UTC')"
"$BACURI_TEST_BIN/bacuri-vms-core" migrate
"$BACURI_TEST_BIN/bacuri-vms-core" migrate
case "${2:-control}" in
    access)
        export BACURI_ORIGIN=https://workspace.test:8443 BACURI_LISTEN=0.0.0.0
        openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc -days 1 -subj /CN=workspace.test \
            -addext 'subjectAltName=DNS:localhost,DNS:workspace.test' -keyout "$BACURI_TLS_KEY" -out "$BACURI_TLS_CERT" 2>/dev/null
        PYTHONDONTWRITEBYTECODE=1 python3 tests/access/scenario.py "$test_dir" ;;
    control)
        ctest --test-dir "$BACURI_TEST_BIN" -R DatabasePool --no-tests=error --output-on-failure
        python3 tests/control/registry.py "$test_dir" ;;
    recovery)
        ctest --test-dir "$BACURI_TEST_BIN" -R PublicationDeathTest --no-tests=error --output-on-failure \
            --output-junit "${BACURI_RECOVERY_REPORT%.json}.boundaries.xml"
        PYTHONDONTWRITEBYTECODE=1 python3 tests/recovery/scenarios.py "$test_dir" ;;
    hold-capacity) PYTHONDONTWRITEBYTECODE=1 python3 tests/archive/hold_capacity.py "$test_dir" ;;
    retention) PYTHONDONTWRITEBYTECODE=1 python3 tests/archive/retention.py "$test_dir" ;;
    archive) PYTHONDONTWRITEBYTECODE=1 python3 tests/archive/recording.py "$test_dir" ;;
    playback|live|monitoring|alerts|workspace|maps|commissioning|batches|edge|investigations|export|policies)
        export BACURI_LISTEN=0.0.0.0 BACURI_ORIGIN="https://$(hostname -i | awk '{print $1}'):8443"
        if [ "$2" = policies ]; then
            export BACURI_CAMERA_NETWORKS="127.0.0.0/8,::1/128,$fixture_ip/32"
            export BACURI_CAMERA_CA_FILE="$BACURI_TLS_CERT" BACURI_TEST_FRAME_COUNTER=1
            PYTHONDONTWRITEBYTECODE=1 python3 tests/archive/policies.py "$test_dir"
        elif [ "$2" = commissioning ] || [ "$2" = batches ] || [ "$2" = edge ]; then
            export BACURI_CAMERA_NETWORKS="127.0.0.0/8,::1/128,$fixture_ip/32"
            export BACURI_CAMERA_CA_FILE="$BACURI_TLS_CERT"
            export BACURI_LEGACY_RTSP_ADDRESSES="$fixture_ip"
            PYTHONDONTWRITEBYTECODE=1 python3 "tests/cameras/$2.py" "$test_dir"
        elif [ "$2" = investigations ]; then
            PYTHONDONTWRITEBYTECODE=1 python3 tests/archive/investigations.py "$test_dir"
        elif [ "$2" = export ]; then
            PYTHONDONTWRITEBYTECODE=1 python3 tests/archive/export.py "$test_dir"
        elif [ "$2" = workspace ] || [ "$2" = maps ]; then
            PYTHONDONTWRITEBYTECODE=1 python3 tests/workspace/scenarios.py "$test_dir" "$2"
        elif [ "$2" = alerts ]; then
            PYTHONDONTWRITEBYTECODE=1 python3 tests/alerts/feed.py "$test_dir"
        elif [ "$2" = monitoring ]; then
            PYTHONDONTWRITEBYTECODE=1 python3 tests/live/monitoring.py "$test_dir"
        elif [ "$2" = live ]; then
            PYTHONDONTWRITEBYTECODE=1 python3 tests/live/streaming.py "$test_dir"
        else
            PYTHONDONTWRITEBYTECODE=1 python3 tests/archive/playback.py "$test_dir"
        fi ;;
    *) exit 2 ;;
esac
