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

#include "four_bytes.cpp"
#include "decrypt.cpp"
#include "inflate.cpp"
#include "pdvout.cpp"
#include "information.cpp"

std::string decryptFile(std::vector<uint_fast8_t>&, uint_fast8_t (&)[XOR_KEY_LENGTH], std::string&);

uint_fast32_t getFourByteValue(const std::vector<uint_fast8_t>&, uint_fast32_t);

void
	startPdv(const std::string&),
	inflateFile(std::vector<uint_fast8_t>&),
	displayInfo();
