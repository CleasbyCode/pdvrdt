#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <string>
#include <regex>
#include <fstream>
#include <zlib.h>

#include "four_bytes.cpp"
#include "crc32.cpp"
#include "erase_chunks.cpp"
#include "encrypt.cpp"
#include "deflate.cpp"
#include "value_updater.cpp"
#include "pdvin.cpp"
#include "information.cpp"

uint32_t
	crcUpdate(uchar*, uint32_t, uint32_t, uint32_t),
	getFourByteValue(const std::vector<uchar>&, uint32_t),
	eraseChunks(std::vector<uchar>&, uint32_t);
void 
	startPdv(std::string&, std::string&, bool, bool),
	encryptFile(std::vector<uchar>&, std::vector<uchar>&, std::string&),
	deflateFile(std::vector<uchar>&),
	valueUpdater(std::vector<uchar>&, uint32_t, const uint32_t, uint8_t),
	displayInfo();

