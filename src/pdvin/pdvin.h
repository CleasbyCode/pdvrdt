#pragma once

#include <vector>
#include <array>
#include <set>
#include <iterator>
#include <filesystem>
#include <random>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <string>
#include <fstream>

// This project uses libsodium (https://libsodium.org/) for cryptographic functions.
#define SODIUM_STATIC
#include <sodium.h>
// Copyright (c) 2013-2025 Frank Denis <github@pureftpd.org>

// https://github.com/madler/zlib
#include <zlib.h>
// Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler

#include "profileVec.cpp"
#include "information.cpp"
#include "programArgs.cpp"
#include "fileChecks.cpp"
#include "writeFile.cpp"
#include "getByteValue.cpp"
#include "crc32.cpp"
#include "searchFunc.cpp"
#include "copyChunks.cpp"
#include "valueUpdater.cpp"
#include "encryptFile.cpp"
#include "deflateFile.cpp"
#include "pdvin.cpp"

template <typename T, size_t N>
uint32_t searchFunc(std::vector<uint8_t>&, uint32_t, const uint8_t, const std::array<T, N>&);

template <typename T>
T getByteValue(const std::vector<uint8_t>&, uint32_t);

bool 	
	isCompressedFile(const std::string&),
	hasValidFilename(const std::string&),
	writeFile(std::vector<uint8_t>&);

uint64_t encryptFile(std::vector<uint8_t>&, std::vector<uint8_t>&, std::string&, bool);

uint32_t crcUpdate(uint8_t*, uint32_t);

void 
	validateFiles(const std::string&, const std::string&, ArgOption),
	deflateFile(std::vector<uint8_t>&, bool, bool),
	valueUpdater(std::vector<uint8_t>&, uint32_t, const uint64_t, uint8_t),
	copyEssentialChunks(std::vector<uint8_t>&, const size_t),
	displayInfo();

uint8_t pdvIn(const std::string&, std::string&, ArgOption, bool);
