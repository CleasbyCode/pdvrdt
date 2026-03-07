// PNG Data Vehicle (pdvrdt v4.7). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023

#pragma once

#include "lodepng/lodepng_config.h"
#include "lodepng/lodepng.h"

#include <zlib.h>
#include <sodium.h>
#include <termios.h>
#include <unistd.h>
#include <sys/types.h>

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
#include <functional>
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

using Key   = std::array<Byte, crypto_secretstream_xchacha20poly1305_KEYBYTES>;
using Salt  = std::array<Byte, crypto_pwhash_SALTBYTES>;

enum class Mode   : Byte { conceal, recover };
enum class Option : Byte { None, Mastodon, Reddit };
