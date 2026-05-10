// PNG Data Vehicle (pdvrdt v4.7). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023

#pragma once

// External library dependencies (included only where needed):
//   lodepng — PNG encoding/decoding (vendored in lodepng/)
//   zlib    — compression           (https://zlib.net)
//   libsodium — cryptography       (https://libsodium.org)

#include <sodium.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using Byte    = std::uint8_t;
using vBytes  = std::vector<Byte>;

using Key   = std::array<Byte, crypto_secretstream_xchacha20poly1305_KEYBYTES>;
using Salt  = std::array<Byte, crypto_pwhash_SALTBYTES>;

enum class Mode   : Byte { conceal, recover };
enum class Option : Byte { None, Mastodon, Reddit };
