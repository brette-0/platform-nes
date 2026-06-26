#!/usr/bin/env bash
# Build the GameCube demo (devkitPPC/libogc).
#   demo/scripts/build-gc.sh [release|debug] [options]   (default: release)
# Requires $DEVKITPRO to be set (devkitPPC + libogc).
set -euo pipefail
exec "$(dirname "${BASH_SOURCE[0]}")/build.sh" gc "$@"
