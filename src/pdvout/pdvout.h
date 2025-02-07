#pragma once

#include <algorithm>
#include <filesystem>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>
#include <iterator>

// This project uses libsodium (https://libsodium.org/) for cryptographic functions.
#define SODIUM_STATIC
#include <sodium.h>
// Copyright (c) 2013-2025 Frank Denis <github@pureftpd.org>

// https://github.com/madler/zlib
#include <zlib.h>
// Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include "valueUpdater.cpp"
#include "getPin.cpp"
#include "getByteValue.cpp"
#include "decryptFile.cpp"
#include "inflateFile.cpp"
#include "pdvout.cpp"
#include "information.cpp"

template <typename T>
T getByteValue(const std::vector<uint8_t>&, uint32_t);

const std::string decryptFile(std::vector<uint8_t>&, bool);

uint64_t getPin();

const uint32_t inflateFile(std::vector<uint8_t>&);

void 
	valueUpdater(std::vector<uint8_t>&, uint32_t, const uint64_t, uint8_t),
	displayInfo();

int pdvOut(const std::string&);
