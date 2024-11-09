#pragma once

#include <vector>
#include <iterator>
#include <filesystem>
#include <random>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <string>
#include <regex>
#include <set>
#include <fstream>
#include <zlib.h>

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

bool 	writeFile(std::vector<uint8_t>&);

const uint32_t encryptFile(std::vector<uint8_t>&, std::vector<uint8_t>&, uint32_t, std::string&, bool);

uint32_t
	deflateFile(std::vector<uint8_t>&, bool, bool),
	crcUpdate(uint8_t*, uint32_t),
	getByteValue(const std::vector<uint8_t>&, const uint32_t);

void 
	valueUpdater(std::vector<uint8_t>&, uint32_t, const uint32_t, uint8_t),
	eraseChunks(std::vector<uint8_t>&, const size_t),
	displayInfo();

int pdvIn(const std::string&, std::string&, bool, bool, bool);
