#pragma once

#include <algorithm>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>
#include <C:\Users\Nick\source\zlib\zlib-1.2.13\zlib.h>

#include "decrypt.cpp"
#include "inflate.cpp"
#include "pdvout.cpp"
#include "information.cpp"

void
	startPdv(std::string&),
	decryptFile(std::vector<uint_fast8_t>&, std::vector<uint_fast8_t>&, std::string&),
	inflateFile(std::vector<uint_fast8_t>&),
	displayInfo();
