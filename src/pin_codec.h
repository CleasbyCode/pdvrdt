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

enum class PinParseError : Byte { malformed, checksum };

// Writes the display form (XXXX-XXXX-XXXX-XXXX-XXXX-XXXX, no terminator) of a
// 128-bit KEY.
void encodeKeyDisplay(const RecoveryPin& pin, KeyDisplay& out);

// Parses what the user typed into `out`. Spaces, tabs and '-' are ignored
// anywhere. Accepts either an old numeric PIN (1-20 digits, no leading zero,
// at most 2^64-1, never 0) or a 24-character KEY whose check pair matches.
// Never allocates; temporaries are wiped. `out` is cleared on failure.
[[nodiscard]] std::expected<void, PinParseError> parseRecoveryPinText(
	std::string_view text, RecoveryPin& out);
