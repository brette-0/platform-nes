#!/usr/bin/env bash
# Build the HTML documentation (Linux/macOS).
#
# Mirrors the Read the Docs build: run Doxygen for both projects, then
# Sphinx. The Windows equivalent is make.bat; docs are normally built on
# Linux only.
#
# Usage: ./docs.sh [sphinx-target]   (default target: html)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT/docs"

: "${SPHINXBUILD:=sphinx-build}"
TARGET="${1:-html}"

for tool in doxygen "$SPHINXBUILD"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: '$tool' not found on PATH." >&2
        echo "  Arch:        sudo pacman -S doxygen graphviz" >&2
        echo "  Sphinx deps: pip install -r docs/requirements.txt" >&2
        exit 1
    fi
done

# Breathe reads the XML these produce (docs/doxygen-api/xml,
# docs/doxygen-advanced/xml); conf.py points at those paths.
doxygen Doxyfile.api
doxygen Doxyfile.advanced

"$SPHINXBUILD" -M "$TARGET" . _build

echo "Docs written to docs/_build/$TARGET"
