#!/usr/bin/env bash
#
# Workhorse build script. Configures (via CMake preset) and builds one target.
#
# Usage:
#   demo/scripts/build.sh <target> [release|debug] [options]
#
#   <target>        nes | linux | win | mac | web |
#                   ctr | gc | wii | switch | wiiu | nds | dsi
#   [release|debug] build type (default: release)
#
# Options:
#   -c, --clean     remove the build directory before configuring
#   -t, --target T  build only the named CMake target (e.g. demo, demo-appimage)
#   -j, --jobs N    parallel build jobs (default: all cores)
#   -h, --help      show this help
#
# The thin wrappers (build-nes.sh, build-linux.sh, ...) just call this.
set -euo pipefail

# Repo root is two levels up now that the scripts live under demo/scripts/.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

usage() { sed -n '2,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

PLATFORM=""
BUILD_TYPE="release"
CLEAN=0
CMAKE_TARGET=""
JOBS=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        nes|linux|win|mac|web|ctr|gc|wii|switch|wiiu|nds|dsi)
                             PLATFORM="$1" ;;
        release|debug)       BUILD_TYPE="$1" ;;
        -c|--clean)          CLEAN=1 ;;
        -t|--target)         CMAKE_TARGET="$2"; shift ;;
        -j|--jobs)           JOBS="$2"; shift ;;
        -h|--help)           usage; exit 0 ;;
        *) echo "error: unknown argument '$1'" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

if [[ -z "$PLATFORM" ]]; then
    echo "error: no target given (nes|linux|win|mac|web|ctr|gc|wii|switch|wiiu|nds|dsi)" >&2
    usage >&2
    exit 2
fi

PRESET="${PLATFORM}-${BUILD_TYPE}"
BUILD_DIR="${ROOT}/cmake-build-${PRESET}"

if [[ "$CLEAN" -eq 1 && -d "$BUILD_DIR" ]]; then
    echo ">> cleaning ${BUILD_DIR}"
    rm -rf "$BUILD_DIR"
fi

echo ">> configuring preset '${PRESET}'"
cmake --preset "$PRESET" -S "$ROOT"

build_args=(--build "$BUILD_DIR")
[[ -n "$CMAKE_TARGET" ]] && build_args+=(--target "$CMAKE_TARGET")
if [[ -n "$JOBS" ]]; then
    build_args+=(--parallel "$JOBS")
else
    build_args+=(--parallel)
fi

echo ">> building preset '${PRESET}'${CMAKE_TARGET:+ (target: ${CMAKE_TARGET})}"
cmake "${build_args[@]}"

echo ">> done -> ${BUILD_DIR}"
