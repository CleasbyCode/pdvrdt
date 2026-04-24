// lodepng build configuration for pdvrdt.
// Disable unused features to reduce compiled code size.
// These must be defined BEFORE including lodepng.h.
//
// Build: g++ -std=c++23 -O2 -o pdvrdt main.cpp src/*.cpp -lz -lsodium
// Note: Do NOT compile lodepng/lodepng.cpp directly; src/lodepng_build.cpp wraps it.

#pragma once

// Use system zlib instead of lodepng's internal implementation (~2,000 lines saved).
// Requires providing custom_zlib callbacks on State objects before decode/encode.
#define LODEPNG_NO_COMPILE_ZLIB

// We do our own file I/O via readFile() (~244 lines saved).
#define LODEPNG_NO_COMPILE_DISK

// We handle PNG chunks (iCCP, IDAT, etc.) manually (~2,134 lines saved).
#define LODEPNG_NO_COMPILE_ANCILLARY_CHUNKS
