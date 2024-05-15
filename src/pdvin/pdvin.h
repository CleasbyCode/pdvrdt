#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <string>
#include <regex>
#include <fstream>
#include <C:\Users\Nick\source\zlib\zlib-1.2.13\zlib.h>

#include "four_bytes.cpp"
#include "crc32.cpp"
#include "erase_chunks.cpp"
#include "encrypt.cpp"
#include "deflate.cpp"
#include "value_updater.cpp"
#include "pdvin.cpp"
#include "information.cpp"

uint_fast32_t
	crcUpdate(uint_fast8_t*, uint_fast32_t, uint_fast32_t, uint_fast32_t),
	getFourByteValue(const std::vector<uint_fast8_t>&, uint_fast32_t),
	eraseChunks(std::vector<uint_fast8_t>&, uint_fast32_t);

void 
	startPdv(std::string&, std::string&, bool, bool),
	encryptFile(std::vector<uint_fast8_t>&, std::vector<uint_fast8_t>&, std::string&),
	deflateFile(std::vector<uint_fast8_t>&),
	valueUpdater(std::vector<uint_fast8_t>&, uint_fast32_t, const uint_fast32_t, uint_fast8_t),
	displayInfo();

