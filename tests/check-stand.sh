#!/bin/sh
set -eu

psql -v ON_ERROR_STOP=1 -c 'SELECT 1'

sh tests/ingest/decode.sh rtsp-camera

test -w /var/lib/bacuri-vms/archive
test -w /var/lib/bacuri-vms/fault-test
df -h /var/lib/bacuri-vms/archive /var/lib/bacuri-vms/fault-test
