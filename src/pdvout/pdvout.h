#pragma once

#include <algorithm>
#include <filesystem>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>
#include <zlib.h>

constexpr uint_fast8_t XOR_KEY_LENGTH = 12;

#include "fourBytes.cpp"
#include "searchFunc.cpp"
#include "decryptFile.cpp"
#include "inflateFile.cpp"
#include "pdvout.cpp"
#include "information.cpp"

std::string decryptFile(std::vector<uint_fast8_t>&, uint_fast8_t (&)[XOR_KEY_LENGTH], std::string&);

template <uint_fast8_t N>
uint_fast32_t searchFunc(std::vector<uint_fast8_t>&, uint_fast32_t, uint_fast8_t, const uint_fast8_t (&)[N]);

uint_fast32_t getFourByteValue(const std::vector<uint_fast8_t>&, const uint_fast32_t);

void
	startPdv(const std::string&),
	inflateFile(std::vector<uint_fast8_t>&),
	displayInfo();
