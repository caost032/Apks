#!/bin/sh
set -eu
# Build through the authoritative Makefile so this capture tool cannot drift behind the
# native engine unit list as new gameplay/world modules are added.
make build/capture_frame
exec ./build/capture_frame "${1:-build/frame.ppm}" "${2:-1280}" "${3:-720}" "${4:-0}"
