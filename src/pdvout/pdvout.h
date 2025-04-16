#pragma once

#include <algorithm>
#include <array>
#include <filesystem>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <iterator>

// This project uses libsodium (https://libsodium.org/) for cryptographic functions.
#define SODIUM_STATIC
#include <C:\Users\Nickc\source\repos\pdvin\libsodium\include\sodium.h>
// Copyright (c) 2013-2025 Frank Denis <github@pureftpd.org>

// https://github.com/madler/zlib
#include <C:\Users\Nickc\source\zlib-1.3.1\zlib.h>
// Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include "valueUpdater.cpp"
#include "information.cpp"
#include "programArgs.cpp"
#include "fileChecks.cpp"
#include "getPin.cpp"
#include "getByteValue.cpp"
#include "decryptFile.cpp"
#include "inflateFile.cpp"
#include "pdvout.cpp"

template <typename T>
T getByteValue(const std::vector<uint8_t>&, uint32_t);

const std::string decryptFile(std::vector<uint8_t>&, bool);

uint64_t getPin();

const uint32_t inflateFile(std::vector<uint8_t>&);

void 
	validateFiles(const std::string&),
	valueUpdater(std::vector<uint8_t>&, uint32_t, const uint64_t, uint8_t),
	displayInfo();

bool hasValidFilename(const std::string&);

uint8_t pdvOut(const std::string&);
