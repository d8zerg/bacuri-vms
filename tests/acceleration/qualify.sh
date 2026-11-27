#!/usr/bin/env bash
set -euo pipefail
qualify() {
    backend=$1
    phase=$2
    if [[ "$phase" == soak ]]; then
        duration=${BACURI_SOAK_SECONDS:-120}
        if ! [[ "$duration" =~ ^[1-9][0-9]{1,2}$ ]] || (( duration < 48 || duration > 600 )); then
            echo "Auto duration must be between 48 and 600 seconds" >&2
            return 2
        fi
    fi
    docker compose run --rm --no-deps -T --user "$(id -u):$(id -g)" -e BUILD_JOBS builder \
        sh tools/build-preset gcc-release --target bacuri-vms-core bacuri-vms-media bacuri-vms-recorder \
        bacuri-pipeline-check bacuri-scheduler-worker
    docker compose build postgres rtsp-camera
    BACURI_DEV_IMAGE=$(docker compose config --images dev)
    BACURI_POSTGRES_IMAGE=$(docker compose config --images postgres)
    BACURI_QUALIFICATION_CAMERA_IMAGE=$(docker compose config --images rtsp-camera)
    export BACURI_DEV_IMAGE BACURI_POSTGRES_IMAGE BACURI_QUALIFICATION_CAMERA_IMAGE
    project_name="bacuri-qualification-$(cat /proc/sys/kernel/random/uuid)"
    project=(docker compose -p "$project_name" -f compose.yaml -f tests/acceleration/compose.yaml)
    run_options=()
    if [[ "$backend" != cpu ]]; then project+=(-f "compose.acceleration-$backend.yaml"); fi
    if [[ "$backend" == vaapi ]]; then run_options+=(--build); fi
    trap '"${project[@]}" --profile tools down --volumes' EXIT
    "${project[@]}" up -d --no-build --wait postgres rtsp-camera
    "${project[@]}" run "${run_options[@]}" --name "$project_name-worker" --rm --no-deps --user "$(id -u):$(id -g)" -T \
        -e BACURI_PROBE_BACKEND="$backend" -e BACURI_QUALIFICATION_PHASE="$phase" -e BACURI_QUALIFICATION_CONTAINER="$project_name-worker" \
        -e BACURI_SOAK_SECONDS -e BACURI_DENSITY_LIMIT -e BACURI_SWITCH_LIMIT \
        -e BACURI_RSS_GROWTH_MIB -e BACURI_VRAM_GROWTH_MIB \
        dev sh tests/check-control.sh gcc-release acceleration-qualification
}

qualify "$@"; exit "$?"
