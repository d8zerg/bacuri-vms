#!/bin/sh
# Desktop sign-in on the running Compose stand: the desktop client waits on its loopback address,
# Chromium signs in on the confirmation page and confirms, the client exchanges the code, reads
# its session with the bearer token and signs out. A second run cancels the sign-in.
set -eu

out=build/desktop/sign-in
mkdir -p "$out"
user="desktop-$(od -An -N4 -tx1 /dev/urandom | tr -d ' \n')"
password="$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')"
printf '%s\n' "$password" | docker compose exec -T core /opt/bacuri/bacuri-vms-core create-user "$user" operator >/dev/null
docker compose exec -T core cat /identity/tls.crt > "$out/site.crt"
image=bacuri-vms-ui-kit-check

run() {
  action=$1
  rm -f "$out/url" "$out/$action.json"
  client=$(docker run -d --network host --user "$(id -u):$(id -g)" -v "$PWD:/workspace" -w /workspace \
    -e QT_QPA_PLATFORM=offscreen -e BACURI_SIGNIN_URL_FILE="$out/url" bacuri-vms-desktop \
    build/desktop/gcc/bacuri-vms-client-signin --site https://localhost:8443 --ca "$out/site.crt" \
    --browser tests/desktop/hand-over-url --timeout 60 --report "$out/$action.json")
  docker run --rm --network host --user "$(id -u):$(id -g)" -v "$PWD/$out:/out" \
    -v "$PWD/tests/desktop/browser.mjs:/workspace/web/client/desktop-browser.mjs:ro" \
    -e URL_FILE=/out/url -e USERNAME="$user" -e PASSWORD="$password" -e ACTION="$action" \
    "$image" node desktop-browser.mjs
  status=$(docker wait "$client")
  docker rm "$client" >/dev/null
  cat "$out/$action.json"
  echo
  echo "$status"
}

allowed=$(run allow | tail -1)
python3 - "$out/allow.json" <<'PY'
import json, sys
report = json.load(open(sys.argv[1]))
assert "error" not in report, report
assert report["token_format"] == "d1 and 64 hexadecimal digits", report
assert report["session_status"] == 200 and report["session_user_matches"], report
assert report["sign_out_status"] == 204 and report["status_after_sign_out"] == 401, report
assert [role["role"] for role in report["site_roles"]] == ["operator"], report
PY
[ "$allowed" = 0 ]
run cancel >/dev/null
python3 -c "import json,sys; r=json.load(open(sys.argv[1])); assert r == {'error': 'access_denied'}, r" "$out/cancel.json"
echo "Desktop sign-in through the browser: confirmed sign-in, bearer session, sign-out and cancellation checked"
