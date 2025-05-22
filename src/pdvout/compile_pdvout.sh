#!/bin/bash

# compile_pdvout.sh

g++ main.cpp programArgs.cpp fileChecks.cpp inflateFile.cpp information.cpp decryptFile.cpp getPin.cpp valueUpdater.cpp pdvOut.cpp -Wall -O2 -lz -lsodium -s -o pdvout

if [ $? -eq 0 ]; then
    echo "Compilation successful. Executable 'pdvout' created."
else
    echo "Compilation failed."
    exit 1
fi
