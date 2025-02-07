#pragma once

#include <vector>
#include <set>
#include <iterator>
#include <filesystem>
#include <random>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <string>
#include <regex>
#include <fstream>

#define SODIUM_STATIC
#include <C:\Users\Nick\source\repos\pdvin\libsodium\include\sodium.h>

// https://github.com/madler/zlib
#include <C:\Users\Nick\source\zlib-1.3.1\zlib.h>
// Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler

#include "profileVec.cpp"
#include "writeFile.cpp"
#include "getByteValue.cpp"
#include "crc32.cpp"
#include "searchFunc.cpp"
#include "eraseChunks.cpp"
#include "valueUpdater.cpp"
#include "encryptFile.cpp"
#include "deflateFile.cpp"
#include "pdvin.cpp"
#include "information.cpp"

template <uint8_t N>
uint32_t searchFunc(std::vector<uint8_t>&, uint32_t, const uint8_t, const uint8_t (&)[N]);

template <typename T>
T getByteValue(const std::vector<uint8_t>&, uint32_t);

bool 	writeFile(std::vector<uint8_t>&);

uint64_t encryptFile(std::vector<uint8_t>&, std::vector<uint8_t>&, std::string&, bool);

uint32_t crcUpdate(uint8_t*, uint32_t);

void 
	deflateFile(std::vector<uint8_t>&, bool, bool),
	valueUpdater(std::vector<uint8_t>&, uint32_t, const uint64_t, uint8_t),
	eraseChunks(std::vector<uint8_t>&, const size_t),
	displayInfo();

int pdvIn(const std::string&, std::string&, ArgOption, bool);
