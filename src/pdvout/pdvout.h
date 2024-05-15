#pragma once

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>
#include <zlib.h>

#include "decrypt.cpp"
#include "inflate.cpp"
#include "pdvout.cpp"
#include "information.cpp"

void
	startPdv(std::string&),
	decryptFile(std::vector<uchar>&, std::vector<uchar>&, std::string&),
	inflateFile(std::vector<uchar>&),
	displayInfo();
