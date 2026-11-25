#!/bin/sh
# Builds both releases (dist/Main, dist/Debug); see ../build.sh.
exec "$(dirname "$0")/../build.sh" "$@"
