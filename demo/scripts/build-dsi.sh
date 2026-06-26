#!/usr/bin/env bash
# Build the Nintendo DSi demo (devkitARM/libnds).
#   demo/scripts/build-dsi.sh [release|debug] [options]   (default: release)
# Requires $DEVKITPRO to be set (devkitARM + libnds).
set -euo pipefail
exec "$(dirname "${BASH_SOURCE[0]}")/build.sh" dsi "$@"
