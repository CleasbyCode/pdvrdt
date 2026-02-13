// PNG Data Vehicle (pdvrdt v4.5). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023

#pragma once

#include "lodepng/lodepng_config.h"
#include "lodepng/lodepng.h"

#ifdef _WIN32
	#include "windows/zlib-1.3.1/include/zlib.h"
	#define SODIUM_STATIC
	#include "windows/libsodium/include/sodium.h"
	#define NOMINMAX
	#include <windows.h>
	#include <conio.h>
#else
	#include <zlib.h>
	#include <sodium.h>
	#include <termios.h>
	#include <unistd.h>
	#include <sys/types.h>
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstdio>
#include <cstdint>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include <unordered_map>

namespace fs = std::filesystem;

using Byte    = std::uint8_t;
using vBytes  = std::vector<Byte>;

using Key   = std::array<Byte, crypto_secretbox_KEYBYTES>;
using Nonce = std::array<Byte, crypto_secretbox_NONCEBYTES>;
using Tag   = std::array<Byte, crypto_secretbox_MACBYTES>;

inline constexpr std::size_t TAG_BYTES = Tag{}.size();

enum class Mode   : Byte { conceal, recover };
enum class Option : Byte { None, Mastodon, Reddit };
