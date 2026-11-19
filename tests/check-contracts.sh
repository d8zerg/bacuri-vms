#!/bin/sh
set -eu

generated=$(mktemp -d)
trap 'rm -rf "$generated"' EXIT
protoc --proto_path=contracts/v1 --python_out="$generated" contracts/v1/control.proto
python3 tests/contracts/check.py "$generated"
