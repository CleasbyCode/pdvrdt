#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CXX="${CXX:-g++}"
OPT_LEVEL="${OPT_LEVEL:--O3}"

case "$OPT_LEVEL" in
  -O0|-O1|-O2|-O3|-Og|-Os|-Oz|-Ofast) ;;
  *)
    echo "Unsupported OPT_LEVEL: $OPT_LEVEL" >&2
    echo "Expected one of: -O0 -O1 -O2 -O3 -Og -Os -Oz -Ofast" >&2
    exit 1
    ;;
esac

COMMON_CXXFLAGS=(
  -std=c++23
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wconversion
  -Wformat
  -Wformat-security
  "$OPT_LEVEL"
  -pipe
  -fstack-protector-strong
  -fstack-clash-protection
  -fPIE
  -s
  -flto=auto
  -DNDEBUG
)

HARDEN_LDFLAGS=(
  -Wl,-z,relro,-z,now
  -pie
)

SOURCES=(
  *.cpp
  lodepng/lodepng_build.cpp
)

LIBS=(
  -lsodium
  -lz
)

build_with_fortify() {
  local fortify_level="$1"
  "$CXX" \
    "${COMMON_CXXFLAGS[@]}" \
    "-D_FORTIFY_SOURCE=${fortify_level}" \
    "${SOURCES[@]}" \
    "${HARDEN_LDFLAGS[@]}" \
    "${LIBS[@]}" \
    -o pdvrdt
}

echo "Compiling pdvrdt (hardened release build, ${OPT_LEVEL})..."
if build_with_fortify 3; then
  echo "Compilation successful. Executable 'pdvrdt' created."
  exit 0
fi

echo "Retrying with -D_FORTIFY_SOURCE=2 for compatibility..."
build_with_fortify 2
echo "Compilation successful. Executable 'pdvrdt' created."
