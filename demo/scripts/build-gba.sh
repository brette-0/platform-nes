#!/usr/bin/env bash
# Build the Game Boy Advance demo (devkitARM/libgba).
#   demo/scripts/build-gba.sh [release|debug] [options]   (default: release)
# Requires $DEVKITPRO to be set (devkitARM + libgba).
set -euo pipefail
exec "$(dirname "${BASH_SOURCE[0]}")/build.sh" gba "$@"
