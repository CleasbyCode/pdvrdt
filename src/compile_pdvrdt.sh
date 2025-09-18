#!/bin/bash

# compile_pdvin.sh

g++ -std=c++20 pdvrdt.cpp lodepng/lodepng.cpp -Wall -O2 -lz -lsodium -s -o pdvrdt

if [ $? -eq 0 ]; then
    echo "Compilation successful. Executable 'pdvrdt' created."
else
    echo "Compilation failed."
    exit 1
fi
