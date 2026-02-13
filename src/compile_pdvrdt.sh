#!/bin/bash

# compile_pdvrdt.sh — Build script for pdvrdt (multi-file layout)

set -e

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++23 -O3 -march=native -pipe -Wall -Wextra -Wpedantic -Wshadow -DNDEBUG -s -flto=auto -fuse-linker-plugin -fstack-protector-strong"
LDFLAGS="-lz -lsodium"
TARGET="pdvrdt"
SRCDIR="."

SOURCES=(
    "$SRCDIR/main.cpp"
    "$SRCDIR/lodepng/lodepng_build.cpp"
    "$SRCDIR/compression.cpp"
    "$SRCDIR/recover.cpp"
    "$SRCDIR/png_utils.cpp"
    "$SRCDIR/io_utils.cpp"
    "$SRCDIR/image.cpp"
    "$SRCDIR/encryption.cpp"
    "$SRCDIR/conceal.cpp"
    "$SRCDIR/args.cpp"
)

echo "Compiling $TARGET..."
$CXX $CXXFLAGS "${SOURCES[@]}" $LDFLAGS -o "$TARGET"
echo "Compilation successful. Executable '$TARGET' created."
