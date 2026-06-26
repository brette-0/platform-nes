#!/usr/bin/env bash
# Build the WebAssembly demo (Emscripten/SDL3).
#   demo/scripts/build-web.sh [release|debug] [options]   (default: release)
# Requires $EMSDK to be set and the Emscripten SDK activated.
set -euo pipefail
exec "$(dirname "${BASH_SOURCE[0]}")/build.sh" web "$@"
