#!/usr/bin/env bash
# Build the Wii demo (devkitPPC/libogc).
#   demo/scripts/build-wii.sh [release|debug] [options]   (default: release)
# Requires $DEVKITPRO to be set (devkitPPC + libogc).
set -euo pipefail
exec "$(dirname "${BASH_SOURCE[0]}")/build.sh" wii "$@"
