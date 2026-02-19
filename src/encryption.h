#pragma once

#include "common.h"

struct ProfileOffsets {
	std::size_t
		filename_xor_key,
		filename,
		sodium_key,     // 32 bytes
		nonce,          // 24 bytes
		encrypted_file;
};

inline constexpr ProfileOffsets
	MASTODON_OFFSETS = { 0x1A6, 0x192, 0x1BE, 0x1DE, 0x1FE },
	DEFAULT_OFFSETS  = { 0x015, 0x001, 0x02D, 0x04D, 0x06E };

[[nodiscard]] std::size_t getPin();

[[nodiscard]] std::size_t encryptDataFile(vBytes& profile_vec, vBytes& data_vec, const std::string& data_filename, bool has_mastodon_option);

[[nodiscard]] std::optional<std::string> decryptDataFile(vBytes& png_vec, bool is_mastodon_file);
