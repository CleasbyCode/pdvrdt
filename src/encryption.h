#pragma once

#include "common.h"

#include <cstddef>
#include <optional>

struct ProfileOffsets {
	std::size_t
		kdf_metadata,   // 56-byte metadata region (magic/alg/salt/nonce/random padding)
		encrypted_file;
};

inline constexpr ProfileOffsets
	MASTODON_OFFSETS = { 0x1BE, 0x1FE },
	DEFAULT_OFFSETS  = { 0x02D, 0x06E };

inline constexpr std::size_t
	KDF_METADATA_REGION_BYTES = 56,
	KDF_MAGIC_OFFSET          = 0,
	KDF_ALG_OFFSET            = 4,
	KDF_SENTINEL_OFFSET       = 5,
	KDF_SALT_OFFSET           = 8,
	KDF_NONCE_OFFSET          = 24;

inline constexpr Byte
	KDF_ALG_ARGON2ID13 = 1,
	KDF_SENTINEL       = 0xA5;

enum class KdfMetadataVersion : Byte {
	none         = 0,
	v2_secretbox = 2
};

[[nodiscard]] std::size_t getPin();

[[nodiscard]] std::size_t encryptDataFile(vBytes& profile_vec, vBytes& data_vec, const std::string& data_filename, bool has_mastodon_option);
[[nodiscard]] std::size_t encryptCompressedFileToProfile(
	vBytes& profile_vec,
	const fs::path& data_file_path,
	const std::string& data_filename,
	Option option,
	bool is_compressed_file,
	bool has_mastodon_option);

[[nodiscard]] std::optional<std::string> decryptDataFile(vBytes& png_vec, bool is_mastodon_file);
