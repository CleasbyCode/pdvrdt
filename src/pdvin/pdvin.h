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
#include <zlib.h>

#include "profileVec.cpp"
#include "fourBytes.cpp"
#include "crc32.cpp"
#include "searchFunc.cpp"
#include "eraseChunks.cpp"
#include "encryptFile.cpp"
#include "deflateFile.cpp"
#include "valueUpdater.cpp"
#include "pdvin.cpp"
#include "information.cpp"

template <uint_fast8_t N>
uint_fast32_t searchFunc(std::vector<uint_fast8_t>&, uint_fast32_t, uint_fast8_t, const uint_fast8_t (&)[N]);

uint_fast32_t
	crcUpdate(uint_fast8_t*, uint_fast32_t),
	encryptFile(std::vector<uint_fast8_t>&, std::vector<uint_fast8_t>&, std::string&),
	deflateFile(std::vector<uint_fast8_t>&),
	getFourByteValue(const std::vector<uint_fast8_t>&, const uint_fast32_t);
	
void 
	startPdv(const std::string&, std::string&, bool, bool),
	eraseChunks(std::vector<uint_fast8_t>&),
	valueUpdater(std::vector<uint_fast8_t>&, uint_fast32_t, const uint_fast32_t, uint_fast8_t),
	displayInfo();

