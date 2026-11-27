#!/bin/sh
# The client bench signs in through Chromium, reads the map scene of the site and shows the
# installed region next to 4 video cells. The tiles come from the site through the local gateway
# that adds the session token. Needs the stand display and GPU and an imported map package.
set -eu

out=build/desktop/site-map
mkdir -p "$out"
rm -f "$out/url" "$out/report.json" "$out/window.png"
user="map-$(od -An -N4 -tx1 /dev/urandom | tr -d ' \n')"
password="$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')"
printf '%s\n' "$password" | docker compose exec -T core /opt/bacuri/bacuri-vms-core create-user "$user" operator >/dev/null
docker compose exec -T core cat /identity/tls.crt > "$out/site.crt"

client=$(docker run -d --network host --gpus all --device /dev/dri --group-add "${BACURI_VIDEO_GID:-44}" \
  --group-add "${BACURI_RENDER_GID:-991}" --user "$(id -u):$(id -g)" -v "$PWD:/workspace" -w /workspace \
  -e DISPLAY="${DISPLAY:-:0}" -e XAUTHORITY=/tmp/bacuri-xauthority -e QT_QPA_PLATFORM=xcb \
  -e NVIDIA_DRIVER_CAPABILITIES=graphics,video,compute,utility -e BACURI_SIGNIN_URL_FILE="$out/url" \
  -e XDG_RUNTIME_DIR=/tmp -v /tmp/.X11-unix:/tmp/.X11-unix:ro -v "${XAUTHORITY:-/dev/null}:/tmp/bacuri-xauthority:ro" \
  bacuri-vms-desktop build/desktop/gcc/bacuri-vms-client-bench \
  --source build/desktop/reference/h264-1080p25.mp4 --codec h264 --cells 4 --seconds 14 \
  --site https://localhost:8443 --ca "$out/site.crt" --browser tests/desktop/hand-over-url \
  --screenshot "$out/window.png" --report "$out/report.json")
docker run --rm --network host --user "$(id -u):$(id -g)" -v "$PWD/$out:/out" \
  -v "$PWD/tests/desktop/browser.mjs:/workspace/web/client/desktop-browser.mjs:ro" \
  -e URL_FILE=/out/url -e USERNAME="$user" -e PASSWORD="$password" -e ACTION=allow \
  bacuri-vms-ui-kit-check node desktop-browser.mjs
status=$(docker wait "$client")
docker logs "$client" 2>&1 | grep -v 'WARNING\|INFO' | tail -5 || true
docker rm "$client" >/dev/null
[ "$status" = 0 ]
python3 - "$out/report.json" <<'PY'
import json, sys
report = json.load(open(sys.argv[1]))
site_map = report["site_map"]
assert site_map["region"]["id"] == "bacuri-test-monaco", site_map
assert site_map["forwarded"] > 0 and site_map["failed"] == 0, site_map
assert report["map"]["renders"] > 0, report["map"]
assert all(not cell["error"] for cell in report["cells_report"]), report["cells_report"]
print(f"Desktop map from the site: region {site_map['region']['id']} revision {site_map['region']['revision']}, "
      f"{site_map['forwarded']} resources through the gateway, map render p50 {report['map']['render_ms']['p50']:.2f} ms")
PY
