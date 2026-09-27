#!/usr/bin/env bash
# Focused PNG scanline bounds and custom zlib adapter regression tests.
# Use PDVRDT_BUILD_MODE=sanitize for ASan/UBSan; CXX selects the compiler.
set -euo pipefail

TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$TESTS/.." && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/pdvrdt-png-decode-limit.XXXXXX")"
trap 'rm -rf -- "$WORK"' EXIT

FLAGS=(-std=c++23 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -D_GLIBCXX_ASSERTIONS)
case "${PDVRDT_BUILD_MODE:-release}" in
    release) FLAGS+=(-O2);;
    sanitize) FLAGS+=(-O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all);;
    *) echo "PDVRDT_BUILD_MODE must be release or sanitize." >&2; exit 2;;
esac

"${CXX:-g++}" "${FLAGS[@]}" -I"$ROOT" \
    "$TESTS/png_decode_limit_test.cpp" \
    "$ROOT/lodepng_crc32.cpp" "$ROOT/lodepng/lodepng_build.cpp" \
    -Wl,--wrap=inflateInit_ -Wl,--wrap=libdeflate_alloc_decompressor \
    -lz -ldeflate -o "$WORK/png_decode_limit_test"
"$WORK/png_decode_limit_test"
