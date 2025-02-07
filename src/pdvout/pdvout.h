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

#define SODIUM_STATIC
#include <C:\Users\Nick\source\repos\pdvin\libsodium\include\sodium.h>

// https://github.com/madler/zlib
#include <C:\Users\Nick\source\zlib-1.3.1\zlib.h>
// Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include "valueUpdater.cpp"
#include "getPin.cpp"
#include "getByteValue.cpp"
#include "decryptFile.cpp"
#include "inflateFile.cpp"
#include "pdvout.cpp"
#include "information.cpp"

template <typename T>
T getByteValue(const std::vector<uint8_t>&, uint32_t);

const std::string decryptFile(std::vector<uint8_t>&, bool);

uint64_t getPin();

const uint32_t inflateFile(std::vector<uint8_t>&);

void 
	valueUpdater(std::vector<uint8_t>&, uint32_t, const uint64_t, uint8_t),
	displayInfo();

int pdvOut(const std::string&);
