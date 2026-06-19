#!/usr/bin/env bash
# Build every target whose preset can configure on this host.
# (win only configures on Windows, mac only on Darwin -- those are skipped
# elsewhere by the preset host condition, so failures there are tolerated.)
#   demo/scripts/build-all.sh [release|debug] [options]   (default: release)
set -uo pipefail
HERE="$(dirname "${BASH_SOURCE[0]}")"

rc=0
for t in nes linux win mac; do
    echo "==================== ${t} ===================="
    if ! "${HERE}/build.sh" "$t" "$@"; then
        echo "!! ${t} build failed or preset unavailable on this host" >&2
        rc=1
    fi
done
exit "$rc"
