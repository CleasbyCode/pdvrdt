#pragma once

#include "common.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>

// The recovery KEY's printed form: 16 bytes read as one big-endian number,
// written as 22 Base58 characters (Bitcoin alphabet, left-padded with '1'),
// then two check characters, shown in six groups of four:
//
//   check = BE32(BLAKE2b-256("pdvrdt key check v1" || key bytes)[0..4]) mod 58^2
//
// Wire format: changing any part of it makes printed KEYs unreadable.
inline constexpr std::size_t KEY_TEXT_CHARS = 24;
inline constexpr std::size_t KEY_DISPLAY_CHARS = KEY_TEXT_CHARS + KEY_TEXT_CHARS / 4 - 1;

using KeyDisplay = std::array<char, KEY_DISPLAY_CHARS>;

enum class KeyParseError : Byte { malformed, checksum };

// Writes the display form (XXXX-XXXX-XXXX-XXXX-XXXX-XXXX, no terminator) of a
// 128-bit KEY.
void encodeKeyDisplay(const RecoveryKey& recovery_key, KeyDisplay& out);

// Parses what the user typed into `out`. Spaces, tabs and '-' are ignored
// anywhere. Accepts only a 24-character KEY whose check pair matches; the
// numeric PINs of images made before pdvrdt 6.0 are malformed. Never allocates;
// temporaries are wiped. `out` is cleared on failure.
[[nodiscard]] std::expected<void, KeyParseError> parseRecoveryKeyText(
	std::string_view text, RecoveryKey& out);
