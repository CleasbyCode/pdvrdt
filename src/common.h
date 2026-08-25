// PNG Data Vehicle (pdvrdt). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023

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
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

// Single source of truth for the release version. Reported by `--info`; bump
// here only. Deliberately not duplicated in CMakeLists.txt or file banners.
inline constexpr std::string_view PDVRDT_VERSION = "5.0";

using Byte    = std::uint8_t;
using vBytes  = std::vector<Byte>;

using Key   = std::array<Byte, crypto_secretstream_xchacha20poly1305_KEYBYTES>;
using Salt  = std::array<Byte, crypto_pwhash_SALTBYTES>;

// Scrub a secret on scope exit.
//
// Construct from any container or byte range (vector, array, string, span) to
// wipe what it holds, or from a plain object to wipe its bytes. The range is
// re-read at destruction rather than captured up front, so a resize -- or a move
// that leaves the source empty -- is honoured instead of scribbling over memory
// the object no longer owns.
//
// sodium_memzero() is specifically guaranteed not to be optimized away, unlike
// an ordinary assignment to a dying object.
template <typename T>
class ScopedWipe {
public:
	explicit ScopedWipe(T& target) noexcept : target_(&target) {}
	ScopedWipe(const ScopedWipe&) = delete;
	ScopedWipe& operator=(const ScopedWipe&) = delete;

	~ScopedWipe() {
		if (target_ == nullptr) return;
		if constexpr (requires { target_->data(); target_->size(); }) {
			if (target_->size() != 0) {
				sodium_memzero(target_->data(), target_->size() * sizeof(*target_->data()));
			}
		} else {
			sodium_memzero(target_, sizeof(T));
		}
	}

	// The bytes are no longer secret here (ownership passed to the caller).
	void release() noexcept { target_ = nullptr; }

private:
	T* target_;
};

template <typename T> ScopedWipe(T&) -> ScopedWipe<T>;

// Wipe a 64-bit secret (e.g. recovery PIN) on scope exit.
//
// Producers of a secret (PIN generation, PIN entry) fill one of these through a
// reference rather than returning std::uint64_t by value: a by-value return
// leaves an unwiped copy of the secret in the return slot that nothing owns.
// For the same reason this type is neither copyable nor movable -- a secret has
// exactly one home, and it is scrubbed when that home goes out of scope.
struct SensitiveU64 {
	std::uint64_t value{};

	SensitiveU64() = default;
	explicit SensitiveU64(std::uint64_t v) noexcept : value(v) {}

	SensitiveU64(const SensitiveU64&) = delete;
	SensitiveU64& operator=(const SensitiveU64&) = delete;
	SensitiveU64(SensitiveU64&&) = delete;
	SensitiveU64& operator=(SensitiveU64&&) = delete;

	// sodium_memzero() is specifically guaranteed not to be optimized away,
	// unlike an ordinary assignment to a dying object.
	~SensitiveU64() {
		sodium_memzero(&value, sizeof(value));
	}
};

enum class Mode   : Byte { conceal, recover };
enum class Option : Byte { None, Mastodon, Reddit };
