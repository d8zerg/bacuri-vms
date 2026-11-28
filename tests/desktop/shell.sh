#!/bin/sh
set -eu
if [ "${BACURI_TEST_BUS:-}" != private ]; then
    runtime=$(mktemp -d)
    trap 'rm -rf "$runtime"' EXIT
    export XDG_RUNTIME_DIR="$runtime"
    if ! getent passwd "$(id -u)" >/dev/null; then
        export NSS_WRAPPER_PASSWD="$runtime/passwd" NSS_WRAPPER_GROUP="$runtime/group"
        cat /etc/passwd > "$NSS_WRAPPER_PASSWD"
        printf 'workstation:x:%s:%s:Workstation:%s:/bin/sh\n' "$(id -u)" "$(id -g)" "$runtime" >> "$NSS_WRAPPER_PASSWD"
        cat /etc/group > "$NSS_WRAPPER_GROUP"
        printf 'workstation:x:%s:\n' "$(id -g)" >> "$NSS_WRAPPER_GROUP"
        export LD_PRELOAD="libnss_wrapper.so${LD_PRELOAD:+:$LD_PRELOAD}"
    fi
    dbus-run-session -- env BACURI_TEST_BUS=private sh "$0" "$@"
    exit
fi
binary=$1
scale=$2
out=$3
shift 3
case "$*" in ""|--update) ;; *) exit 2 ;; esac
reference_mode=${1:-}
temp=$(mktemp -d)
trap 'rm -rf "$temp"' EXIT
export XDG_CURRENT_DESKTOP=KDE
export XDG_CONFIG_HOME="$temp/config" XDG_RUNTIME_DIR="$temp" GSETTINGS_BACKEND=memory
export QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 QT_QPA_PLATFORM=xcb
result="$out/$scale"
export BACURI_SHELL_REPORT="$temp/report" BACURI_SHELL_VIDEO="$temp/reference.mp4"
mkdir -p "$BACURI_SHELL_REPORT" "$result"
publish() { cp -a "$BACURI_SHELL_REPORT/." "$result/"; }
python3 "$(dirname "$0")/appearance_portal.py" &
portal_pid=$!
trap 'publish; kill "$portal_pid" 2>/dev/null || :; rm -rf "$temp"' EXIT
gdbus wait --session --timeout 8 org.freedesktop.portal.Desktop
if [ "$scale" = mixed ]; then
    cat > "$temp/screens.json" <<'JSON'
{"screens":[{"name":"standard","width":1920,"height":1080,"logicalDpi":96},
            {"name":"high","x":1920,"width":3840,"height":2160,"logicalDpi":192}]}
JSON
    export QT_QPA_PLATFORM="offscreen:configfile=$temp/screens.json" QT_QUICK_BACKEND=software
    "$binary" mixedDpi
    python3 "$(dirname "$0")/references.py" "$BACURI_SHELL_REPORT" "$scale" ${reference_mode:+"$reference_mode"}
    exit
fi
if [ "$scale" = workstation ]; then
    export BACURI_SHELL_VIDEO=/workspace/build/desktop/reference/h264-1080p25.mp4
else
    gst-launch-1.0 -qe videotestsrc num-buffers=750 ! video/x-raw,width=320,height=180,framerate=25/1 \
        ! x264enc key-int-max=25 bframes=0 speed-preset=ultrafast ! h264parse ! mp4mux ! filesink location="$BACURI_SHELL_VIDEO"
    export QT_SCALE_FACTOR="$scale" LIBGL_ALWAYS_SOFTWARE=1
fi
run() {
    if [ "$scale" = workstation ]; then "$binary" "$@"; return; fi
    xvfb-run -a -s '-screen 0 2400x1800x24' "$binary" "$@"
}
run productWindow
if [ "$scale" = 1 ] || [ "$scale" = workstation ]; then run modelsAndIntents componentPreview inputLatency; fi
run catalogAndPlayback
if [ "$scale" = workstation ]; then run displayMetrics; fi
python3 "$(dirname "$0")/contrast.py" "$BACURI_SHELL_REPORT"
if [ "$scale" = 1 ]; then
    export BACURI_TEST_CA="$temp/site.crt"
    openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc -subj /CN=localhost \
        -addext subjectAltName=IP:127.0.0.1 -keyout "$temp/site.key" -out "$BACURI_TEST_CA" 2>/dev/null
    export BACURI_TEST_PEER="$(dirname "$binary")/bacuri-preview-test-peer"
    export BACURI_COMMAND_FILES="$temp/files" BACURI_EXPORT_DIRECTORY="$temp/files" BACURI_CAPTURE_DIRECTORY="$temp/files"
    mkdir -p "$BACURI_COMMAND_FILES"
    python3 "$(dirname "$0")/workspace_site.py" "$BACURI_TEST_CA" "$temp/site.key" > "$temp/origin" &
    server=$!
    trap 'publish; kill "$portal_pid" "$server" 2>/dev/null || :; wait "$server" 2>/dev/null || :; rm -rf "$temp"' EXIT
    for iteration in $(seq 100); do [ -s "$temp/origin" ] && break; sleep 0.05; done
    BACURI_TEST_SITE=$(cat "$temp/origin")
    export BACURI_TEST_SITE
    run commandTimes commandProfiles siteSession commandWorkflow largePalette
    xvfb-run -a python3 "$(dirname "$0")/atspi.py" "$(dirname "$binary")/bacuri-vms-client-catalog" "$BACURI_SHELL_REPORT/atspi.json"
fi
if [ "$scale" != workstation ]; then
    python3 "$(dirname "$0")/references.py" "$BACURI_SHELL_REPORT" "$scale" ${reference_mode:+"$reference_mode"}
fi
