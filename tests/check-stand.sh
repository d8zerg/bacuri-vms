#!/bin/sh
set -eu

psql -v ON_ERROR_STOP=1 -c 'SELECT 1'

check_stream() {
    timeout 20s gst-launch-1.0 -q \
        rtspsrc location="rtsp://rtsp-camera:8554/$1" protocols=tcp latency=100 \
        ! rtph264depay ! avdec_h264 ! "video/x-raw,width=$2,height=$3" \
        ! fakesink num-buffers=25 sync=false
}

check_stream main 1280 720
check_stream sub 640 360

test -w /var/lib/bacuri-vms/archive
test -w /var/lib/bacuri-vms/fault-test
df -h /var/lib/bacuri-vms/archive /var/lib/bacuri-vms/fault-test
