#!/usr/bin/env bash
# Build the Nintendo DS demo (devkitARM/libnds).
#   demo/scripts/build-nds.sh [release|debug] [options]   (default: release)
# Requires $DEVKITPRO to be set (devkitARM + libnds).
set -euo pipefail
exec "$(dirname "${BASH_SOURCE[0]}")/build.sh" nds "$@"
