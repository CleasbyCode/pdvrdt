// PNG Data Vehicle (pdvrdt v4.7). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023

#pragma once

// ---------------------------------------------------------------------------
// Platform + toolchain floor. pdvrdt is distributed as source and built by
// each user, so fail early with a clear message rather than a wall of errors
// when the environment is unsupported.
//
// Platform: Linux only (renameat2(RENAME_NOREPLACE), termios PIN entry, etc.).
// Toolchain: features that set this floor:
//   - std::print / std::println          (GCC 14, Clang 18 + libc++ 18)
//   - std::format, std::ranges, [[assume]] (GCC 13+ / Clang 19)
// A matching C++23 standard library is required.
// ---------------------------------------------------------------------------
#if !defined(__linux__)
#  error "pdvrdt requires Linux."
#endif
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ < 14
#  error "pdvrdt requires GCC >= 14 (for std::print/std::format). Please upgrade your compiler."
#elif defined(__clang__) && __clang_major__ < 18
#  error "pdvrdt requires Clang >= 18 with a C++23 standard library (libc++ 18+ or libstdc++ 14+). Please upgrade your compiler."
#endif

// External library dependencies (included only where needed):
//   lodepng — PNG encoding/decoding (vendored in lodepng/)
//   zlib    — compression           (https://zlib.net)
//   libsodium — cryptography       (https://libsodium.org)

#include <sodium.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using Byte    = std::uint8_t;
using vBytes  = std::vector<Byte>;

using Key   = std::array<Byte, crypto_secretstream_xchacha20poly1305_KEYBYTES>;
using Salt  = std::array<Byte, crypto_pwhash_SALTBYTES>;

// Wipe a 64-bit secret (e.g. recovery PIN) on scope exit. Use release() only
// when transferring ownership to another SensitiveU64 or an intentional wipe.
struct SensitiveU64 {
	std::uint64_t value{};
	bool active{true};

	SensitiveU64() = default;
	explicit SensitiveU64(std::uint64_t v) noexcept : value(v) {}

	SensitiveU64(const SensitiveU64&) = delete;
	SensitiveU64& operator=(const SensitiveU64&) = delete;
	SensitiveU64(SensitiveU64&&) = delete;
	SensitiveU64& operator=(SensitiveU64&&) = delete;

	~SensitiveU64() {
		if (active) {
			sodium_memzero(&value, sizeof(value));
		}
	}

	// Copy out the value and stop wiping this object (caller owns the secret).
	[[nodiscard]] std::uint64_t release() noexcept {
		const std::uint64_t v = value;
		// An ordinary assignment may be discarded by LTO because this object is
		// made inactive immediately afterwards.  sodium_memzero() is specifically
		// guaranteed not to be optimized away.
		sodium_memzero(&value, sizeof(value));
		active = false;
		return v;
	}
};

enum class Mode   : Byte { conceal, recover };
enum class Option : Byte { None, Mastodon, Reddit };

[[nodiscard]] std::uint32_t pdvrdtCrc32Update(
	std::uint32_t crc,
	std::span<const Byte> data) noexcept;
