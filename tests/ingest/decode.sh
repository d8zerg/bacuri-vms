#!/bin/sh
set -eu

decode() {
    timeout 25s gst-launch-1.0 -q \
        rtspsrc location="rtsp://$1:8554/$2" protocols=tcp latency=250 \
        ! rtph264depay ! avdec_h264 ! "video/x-raw,width=$3,height=$4" \
        ! fakesink num-buffers=25 sync=false
}

decode "$1" main 1280 720 &
main=$!
decode "$1" sub 640 360 &
sub=$!
result=0
wait "$main" || result=1
wait "$sub" || result=1
exit "$result"
