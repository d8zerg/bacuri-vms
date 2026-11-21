#!/usr/bin/env bash
set -euo pipefail
docker compose build postgres rtsp-camera playback-firefox
BACURI_DEV_IMAGE=$(docker compose config --images dev)
BACURI_POSTGRES_IMAGE=$(docker compose config --images postgres)
BACURI_WORKSPACE_CAMERA_IMAGE=$(docker compose config --images rtsp-camera)
BACURI_WORKSPACE_FIREFOX_IMAGE=$(docker compose config --images playback-firefox)
export BACURI_DEV_IMAGE BACURI_POSTGRES_IMAGE BACURI_WORKSPACE_CAMERA_IMAGE BACURI_WORKSPACE_FIREFOX_IMAGE
project=(docker compose -p "bacuri-workspace-$(cat /proc/sys/kernel/random/uuid)" -f compose.yaml -f tests/workspace/compose.yaml)
trap '"${project[@]}" --profile playback-test down --volumes' EXIT
services=(postgres rtsp-camera playback-chrome)
if [ "${3:-maps}" = maps ]; then services+=(playback-firefox); fi
"${project[@]}" up -d --no-build --wait "${services[@]}"
"${project[@]}" run --rm --no-deps -T --user "$(id -u):$(id -g)" -e "BACURI_WORKSPACE_RESULTS=/workspace/$2" dev sh tests/check-control.sh "$1" "${3:-maps}"
