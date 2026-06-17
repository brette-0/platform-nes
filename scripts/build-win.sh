#!/usr/bin/env bash
# Build the Windows desktop demo (SDL3). The win preset only configures on a
# Windows host (e.g. Git Bash / MSYS2); it is a no-op elsewhere.
#   scripts/build-win.sh [release|debug] [options]   (default: release)
set -euo pipefail
exec "$(dirname "${BASH_SOURCE[0]}")/build.sh" win "$@"
