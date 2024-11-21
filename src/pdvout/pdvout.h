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
#include <C:\Users\Nick\source\zlib-1.3.1\zlib.h>

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include "valueUpdater.cpp"
#include "crc32.cpp"
#include "getPin.cpp"
#include "getByteValue.cpp"
#include "decryptFile.cpp"
#include "inflateFile.cpp"
#include "pdvout.cpp"
#include "information.cpp"

const std::string decryptFile(std::vector<uint8_t>&, std::vector<uint8_t>&, bool);

uint32_t 
	crcUpdate(uint8_t*, uint32_t),
	getByteValue(const std::vector<uint8_t>&, const uint32_t),
	getPin();

const uint32_t inflateFile(std::vector<uint8_t>&);

void 
	valueUpdater(std::vector<uint8_t>&, uint32_t, const uint32_t, uint8_t),
	displayInfo();

int pdvOut(const std::string&);
