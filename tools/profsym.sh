#!/usr/bin/env bash
#
# profsym.sh -- resolve OGC PC-sampler hex addresses to function + file:line.
#
# Read the 8-digit hex addresses off the on-screen profiler overlay (top = hottest)
# and pass them here. They are looked up in the linked GameCube/Wii ELF's symbols.
#
#   tools/profsym.sh 8003a1c0 8004b220 80051f0c
#   tools/profsym.sh path/to/demo.elf 8003a1c0           # explicit ELF
#
# Defaults to the docker GC build's ELF. The matching ELF is required: the .dol
# you booted must come from this exact ELF or the addresses won't line up.
set -euo pipefail

ELF="cmake-build-ci-gc-release/demo.elf"
ADDR2LINE="${DEVKITPRO:-/opt/devkitpro}/devkitPPC/bin/powerpc-eabi-addr2line"

args=()
for a in "$@"; do
    if [[ "$a" == *.elf ]]; then ELF="$a"; else args+=("0x${a#0x}"); fi
done

[[ -x "$ADDR2LINE" ]] || { echo "addr2line not found: $ADDR2LINE" >&2; exit 1; }
[[ -f "$ELF" ]]       || { echo "ELF not found: $ELF (build it, or pass the path)" >&2; exit 1; }
[[ ${#args[@]} -gt 0 ]] || { echo "usage: $0 [demo.elf] <hexaddr> [hexaddr ...]" >&2; exit 1; }

# -f function names, -C demangle, -i show inlined frames.
"$ADDR2LINE" -e "$ELF" -f -C -i "${args[@]}"
