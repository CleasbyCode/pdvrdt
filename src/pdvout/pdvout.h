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
#include <zlib.h>

#include "getByteValue.cpp"
#include "searchFunc.cpp"
#include "decryptFile.cpp"
#include "inflateFile.cpp"
#include "pdvout.cpp"
#include "information.cpp"

std::string decryptFile(std::vector<uint8_t>&, const uint8_t*, const std::string&);

template <uint8_t N>
uint32_t searchFunc(std::vector<uint8_t>&, uint32_t, uint8_t, const uint8_t (&)[N]);

uint32_t getByteValue(const std::vector<uint8_t>&, const uint32_t);

void
	inflateFile(std::vector<uint8_t>&),
	displayInfo();

uint8_t pdvOut(const std::string&);
