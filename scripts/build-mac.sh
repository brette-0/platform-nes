#!/usr/bin/env bash
# Build the macOS desktop demo (SDL3). The mac preset only configures on a
# Darwin host; it is a no-op elsewhere.
#   scripts/build-mac.sh [release|debug] [options]   (default: release)
set -euo pipefail
exec "$(dirname "${BASH_SOURCE[0]}")/build.sh" mac "$@"
