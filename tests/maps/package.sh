#!/bin/sh
# Imports the Monaco map package into the running Compose stand and checks that the site serves
# its scene, style and tiles. The package is built by task maps:fixture.
set -eu

package=build/maps/monaco.mbtiles
out=build/maps/check
mkdir -p "$out"
docker compose run --rm --no-deps -T -v "$PWD/$package:/import/monaco.mbtiles:ro" \
  core /opt/bacuri/bacuri-vms-core import-map /import/monaco.mbtiles
user="maps-$(od -An -N4 -tx1 /dev/urandom | tr -d ' \n')"
password="$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')"
printf '%s\n' "$password" | docker compose exec -T core /opt/bacuri/bacuri-vms-core create-user "$user" operator >/dev/null
docker compose exec -T core cat /identity/tls.crt > "$out/site.crt"
docker run --rm --network host --user "$(id -u):$(id -g)" -v "$PWD:/workspace" -w /workspace bacuri-vms-desktop \
  python3 tests/maps/package_check.py "$package" --site https://localhost:8443 --ca "$out/site.crt" \
  --user "$user" --password "$password"
