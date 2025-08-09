#!/bin/bash

# compile_pdvin.sh

g++ -std=c++20 main.cpp lodepng/lodepng.cpp programArgs.cpp fileChecks.cpp \
    information.cpp \
    profileVec.cpp  \
    -Wall -O3 -lz -lsodium -s -o pdvrdt

if [ $? -eq 0 ]; then
    echo "Compilation successful. Executable 'pdvrdt' created."
else
    echo "Compilation failed."
    exit 1
fi
