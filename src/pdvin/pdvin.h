#pragma once

#include <vector>
#include <filesystem>
#include <random>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <string_view>
#include <string>
#include <regex>
#include <fstream>
#include <C:\Users\Nick\source\zlib-1.3.1\zlib.h>

#include "profileVec.cpp"
#include "getByteValue.cpp"
#include "getVecSize.cpp"
#include "crc32.cpp"
#include "searchFunc.cpp"
#include "eraseChunks.cpp"
#include "encryptFile.cpp"
#include "deflateFile.cpp"
#include "valueUpdater.cpp"
#include "pdvin.cpp"
#include "information.cpp"

template <uint_fast8_t N>
uint_fast32_t searchFunc(std::vector<uint_fast8_t>&, uint_fast32_t, const uint_fast8_t, const uint_fast8_t (&)[N]);

uint_fast32_t
	crcUpdate(uint_fast8_t*, uint_fast32_t),
	getVecSize(const std::vector<uint_fast8_t>&),
	encryptFile(std::vector<uint_fast8_t>&, std::vector<uint_fast8_t>&, std::string&),
	deflateFile(std::vector<uint_fast8_t>&, const std::string),
	getByteValue(const std::vector<uint_fast8_t>&, const uint_fast32_t);
	
bool eraseChunks(std::vector<uint_fast8_t>&);

void 
	valueUpdater(std::vector<uint_fast8_t>&, uint_fast32_t, const uint_fast32_t, uint_fast8_t),
	displayInfo();

uint_fast8_t pdvIn(const std::string&, std::string&, bool, bool);