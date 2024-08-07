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

template <uint8_t N>
uint32_t searchFunc(std::vector<uint8_t>&, uint32_t, uint8_t, const uint8_t (&)[N]);

uint32_t
	crcUpdate(uint8_t*, uint32_t),
	encryptFile(std::vector<uint8_t>&, std::vector<uint8_t>&, std::string&),
	deflateFile(std::vector<uint8_t>&, const std::string),
	getFourByteValue(const std::vector<uint8_t>&, const uint32_t);
	
void 
	startPdv(const std::string&, std::string&, bool, bool),
	eraseChunks(std::vector<uint8_t>&),
	valueUpdater(std::vector<uint8_t>&, uint32_t, const uint32_t, uint8_t),
	displayInfo();

