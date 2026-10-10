#!/usr/bin/env bash
# Copyright (c) 2026 HotKV Ltd
# Licensed under the Apache License, Version 2.0. See LICENSE in the project root.
#
# Builds hotkv-cpp, installs it to a staging prefix, MOVES that prefix to a
# different location (proving the install tree is relocatable), then builds
# and runs tests/consumer against it twice: through find_package(CONFIG) and
# through pkg-config. Usage:
#
#   scripts/verify-install.sh <workdir> [extra cmake configure args...]
#
# Example: scripts/verify-install.sh /tmp/v1 -DBUILD_SHARED_LIBS=ON -DHOTKV_CPP_WITH_TLS=OFF
set -euo pipefail

src="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$1"; shift
rm -rf "$work"; mkdir -p "$work"
work="$(cd "$work" && pwd)"

cmake -S "$src" -B "$work/build" -DCMAKE_BUILD_TYPE=Release -DHOTKV_CPP_BUILD_TESTS=OFF "$@" > "$work/configure.log"
cmake --build "$work/build" -j > "$work/build.log"
cmake --install "$work/build" --prefix "$work/stage" > "$work/install.log"

# Relocate: nothing may remember the staging path.
mv "$work/stage" "$work/relocated"
prefix="$work/relocated"
if grep -rIl "$work/stage" "$prefix" 2>/dev/null; then
    echo "FAIL: install tree still references the staging prefix" >&2
    exit 1
fi

# 1. find_package(CONFIG)
cmake -S "$src/tests/consumer" -B "$work/consumer-cmake" -DCMAKE_PREFIX_PATH="$prefix" \
    -DCMAKE_BUILD_TYPE=Release > "$work/consumer-cmake.log"
cmake --build "$work/consumer-cmake" >> "$work/consumer-cmake.log"
"$work/consumer-cmake/consumer"

# 2. pkg-config (skipped where pkg-config is not installed)
if command -v pkg-config >/dev/null 2>&1 && pkg-config --version >/dev/null 2>&1; then
    pcdir="$(dirname "$(find "$prefix" -name hotkv-cpp.pc | head -n1)")"
    export PKG_CONFIG_PATH="$pcdir"
    flags="--cflags --libs"
    if ! ls "$prefix"/lib*/libhotkv-cpp.so* "$prefix"/lib*/*/libhotkv-cpp.so* >/dev/null 2>&1; then
        flags="--static --cflags --libs"   # static library: pull Libs.private/Requires.private
    fi
    # shellcheck disable=SC2046
    ${CXX:-c++} -std=c++20 "$src/tests/consumer/main.cpp" -o "$work/consumer-pc" $(pkg-config $flags hotkv-cpp)
    libdir="$(dirname "$(find "$prefix" -name 'libhotkv-cpp.*' | head -n1)")"
    LD_LIBRARY_PATH="$libdir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$work/consumer-pc"
    echo "pkg-config prefix: $(pkg-config --variable=prefix hotkv-cpp)"
else
    echo "pkg-config not installed; skipped the pkg-config leg"
fi
echo "verify-install OK ($*)"
