#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/.."

export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
if [ ! -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]; then
    printf 'vcpkg toolchain not found in %s. Install and bootstrap vcpkg there, or set VCPKG_ROOT.\n' "$VCPKG_ROOT" >&2
    exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
    printf '%s\n' 'Homebrew is required. Install Homebrew, then run: brew install cmake llvm libomp pkg-config' >&2
    exit 1
fi

llvm_root="$(brew --prefix llvm)"
openmp_root="$(brew --prefix libomp)"
export CC="${CC:-$llvm_root/bin/clang}"
export CXX="${CXX:-$llvm_root/bin/clang++}"
export CMAKE_PREFIX_PATH="$openmp_root${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"

cmake -S . -B build/macos -G 'Unix Makefiles' \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVOXELLANCER_BUILD_CLIENT=ON \
    -DVOXELLANCER_BUILD_SERVER=ON
cmake --build build/macos --config Release --parallel 8 --target voxellancer voxellancer-server
exec ./build/macos/voxellancer -fullscreen -local-singleplayer "$@"
