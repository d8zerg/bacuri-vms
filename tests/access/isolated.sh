#!/usr/bin/env bash
set -euo pipefail
BACURI_DEV_IMAGE=$(docker compose config --images dev)
BACURI_CAMERA_IMAGE=$(docker compose config --images rtsp-camera)
BACURI_POSTGRES_IMAGE=$(docker compose config --images postgres)
BACURI_ACCESS_STAND=$(mktemp -d "$PWD/build/access-stand-XXXXXXXX")
BACURI_TEST_IDP_PASSWORD=$(cat /proc/sys/kernel/random/uuid)
export BACURI_DEV_IMAGE BACURI_CAMERA_IMAGE BACURI_POSTGRES_IMAGE BACURI_ACCESS_STAND BACURI_TEST_IDP_PASSWORD
project=(docker compose --project-directory "$PWD" -p "bacuri-access-$(cat /proc/sys/kernel/random/uuid)" -f tests/access/compose.yaml)
cleanup() {
    "${project[@]}" down -v
    rm -rf -- "$BACURI_ACCESS_STAND"
}
trap cleanup EXIT
mkdir -p "$BACURI_ACCESS_STAND/keycloak"
touch "$BACURI_ACCESS_STAND/keycloak/realm.json"
"${project[@]}" run --rm --no-deps -T --user "$(id -u):$(id -g)" runner python3 tests/access/realm.py
"${project[@]}" up -d --wait postgres keycloak rtsp-camera playback-chrome
"${project[@]}" run --rm --use-aliases --no-deps -T --user "$(id -u):$(id -g)" runner sh tests/check-control.sh "${1:-gcc-debug}" access &
runner=$!
while kill -0 "$runner" 2>/dev/null && ! test -f "$BACURI_ACCESS_STAND/outage"; do sleep 1; done
if test -f "$BACURI_ACCESS_STAND/outage"; then
    # Keep the issuer's DNS entry and TCP endpoint while application responses are unavailable.
    "${project[@]}" pause keycloak
    touch "$BACURI_ACCESS_STAND/offline"
    while kill -0 "$runner" 2>/dev/null && ! test -f "$BACURI_ACCESS_STAND/restore"; do sleep 1; done
    "${project[@]}" unpause keycloak
fi
wait "$runner"
