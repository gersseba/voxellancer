#!/bin/bash
set -euo pipefail

if [[ -z "${VCPKG_ROOT:-}" || ! -x "$VCPKG_ROOT/vcpkg" ]]; then
    echo "Set VCPKG_ROOT to a bootstrapped vcpkg checkout before running this script." >&2
    exit 1
fi

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"
git submodule update --init
"$VCPKG_ROOT/vcpkg" install --triplet "${VCPKG_DEFAULT_TRIPLET:-x64-linux}"
