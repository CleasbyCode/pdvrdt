#pragma once

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>
#include <C:\Users\Nick\source\zlib-1.3.1\zlib.h>

constexpr uint_fast8_t XOR_KEY_LENGTH = 12;

#include "decrypt.cpp"
#include "inflate.cpp"
#include "pdvout.cpp"
#include "information.cpp"

void
	startPdv(std::string&),
	decryptFile(std::vector<uint_fast8_t>&, std::vector<uint_fast8_t>&, uint_fast8_t (&)[XOR_KEY_LENGTH], std::string&),
	inflateFile(std::vector<uint_fast8_t>&),
	displayInfo();
