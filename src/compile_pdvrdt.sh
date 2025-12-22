#!/bin/bash

# compile_pdvin.sh

g++ -std=c++23 -O3 -march=native -pipe -Wall -Wextra -Wpedantic -DNDEBUG -s -flto=auto -fuse-linker-plugin pdvrdt.cpp lodepng/lodepng.cpp -lz -lsodium -o pdvrdt

if [ $? -eq 0 ]; then
    echo "Compilation successful. Executable 'pdvrdt' created."
else
    echo "Compilation failed."
    exit 1
fi
