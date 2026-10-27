#!/usr/bin/env bash
# Recovery KEY codec: display/parse vectors, check characters, old numeric PINs.
# Use PDVRDT_BUILD_MODE=sanitize for ASan/UBSan; CXX selects the compiler.
set -euo pipefail

TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$TESTS/.." && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/pdvrdt-pin-codec.XXXXXX")"
trap 'rm -rf -- "$WORK"' EXIT

FLAGS=(-std=c++23 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -D_GLIBCXX_ASSERTIONS)
case "${PDVRDT_BUILD_MODE:-release}" in
    release) FLAGS+=(-O2);;
    sanitize) FLAGS+=(-O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all);;
    *) echo "PDVRDT_BUILD_MODE must be release or sanitize." >&2; exit 2;;
esac

"${CXX:-g++}" "${FLAGS[@]}" -I"$ROOT" \
    "$TESTS/pin_codec_test.cpp" "$ROOT/pin_codec.cpp" \
    -lsodium -o "$WORK/pin_codec_test"
"$WORK/pin_codec_test"
