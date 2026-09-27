#!/usr/bin/env bash
# Focused carrier v2 KDF, downgrade rejection, and v1 compatibility tests.
# Use PDVRDT_BUILD_MODE=sanitize for ASan/UBSan; CXX selects the compiler.
set -euo pipefail

TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$TESTS/.." && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/pdvrdt-carrier-kdf.XXXXXX")"
trap 'rm -rf -- "$WORK"' EXIT

FLAGS=(-std=c++23 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -D_GLIBCXX_ASSERTIONS)
case "${PDVRDT_BUILD_MODE:-release}" in
    release) FLAGS+=(-O2);;
    sanitize) FLAGS+=(-O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all);;
    *) echo "PDVRDT_BUILD_MODE must be release or sanitize." >&2; exit 2;;
esac

# carrier_kdf_test.cpp includes reddit_steg.cpp to inspect and corrupt headers;
# do not add reddit_steg.cpp to this list as a second translation unit.
"${CXX:-g++}" "${FLAGS[@]}" -I"$ROOT" \
    "$TESTS/carrier_kdf_test.cpp" \
    "$ROOT/encryption.cpp" "$ROOT/compression.cpp" "$ROOT/io_utils.cpp" \
    "$ROOT/png_utils.cpp" "$ROOT/lodepng_crc32.cpp" "$ROOT/lodepng/lodepng_build.cpp" \
    -Wl,--wrap=crypto_pwhash -Wl,--wrap=crypto_generichash \
    -lsodium -lz -ldeflate -o "$WORK/carrier_kdf_test"
"$WORK/carrier_kdf_test" "$TESTS"
