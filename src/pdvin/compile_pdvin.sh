#!/bin/bash

# compile_pdvin.sh

g++ main.cpp programArgs.cpp fileChecks.cpp copyChunks.cpp deflateFile.cpp \
    information.cpp writeFile.cpp encryptFile.cpp valueUpdater.cpp \
    crc32.cpp profileVec.cpp pdvIn.cpp \
    -Wall -O2 -lz -lsodium -s -o pdvin

if [ $? -eq 0 ]; then
    echo "Compilation successful. Executable 'pdvin' created."
else
    echo "Compilation failed."
    exit 1
fi
