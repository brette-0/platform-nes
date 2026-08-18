#!/usr/bin/env bash
# Build every target whose preset can configure on this host.
# (win only configures on Windows, mac only on Darwin, web requires $EMSDK,
# devkitPro targets require $DEVKITPRO -- all skipped when unavailable.)
#   demo/scripts/build-all.sh [release|debug] [options]   (default: release)
set -uo pipefail
HERE="$(dirname "${BASH_SOURCE[0]}")"

rc=0
for t in nes linux win mac web ctr gc wii switch wiiu nds dsi; do
    echo "==================== ${t} ===================="
    if ! "${HERE}/build.sh" "$t" "$@"; then
        echo "!! ${t} build failed or preset unavailable on this host" >&2
        rc=1
    fi
done
exit "$rc"
