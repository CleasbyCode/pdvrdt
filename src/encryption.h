#pragma once

#include "common.h"

#include <cstddef>
#include <cstring>
#include <optional>
#include <span>

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

inline constexpr auto KDF_METADATA_MAGIC_V2 =
	std::to_array<Byte>({'K', 'D', 'F', '2'});

inline constexpr auto PDVRDT_SIG =
	std::to_array<Byte>({0xC6, 0x50, 0x3C, 0xEA, 0x5E, 0x9D, 0xF9});

inline constexpr std::size_t DEFAULT_PDV_SIG_OFFSET  = 101;
inline constexpr std::size_t MASTODON_PDV_SIG_OFFSET = 502;
// Recovery never inflates an iCCP profile beyond this size. Conceal uses the
// same ceiling while building the pre-compression Mastodon profile so it can
// never emit an image that its own recovery path refuses to process.
inline constexpr std::size_t MAX_MASTODON_PROFILE_BYTES = 64ULL * 1024 * 1024;

[[nodiscard]] inline bool hasSupportedKdfMetadataAt(std::span<const Byte> data, std::size_t base_index) {
	if (base_index > data.size() || KDF_METADATA_REGION_BYTES > data.size() - base_index) {
		return false;
	}

	const std::size_t magic_offset = base_index + KDF_MAGIC_OFFSET;
	return
		std::memcmp(
			data.data() + magic_offset,
			KDF_METADATA_MAGIC_V2.data(),
			KDF_METADATA_MAGIC_V2.size()
		) == 0 &&
		data[base_index + KDF_ALG_OFFSET] == KDF_ALG_ARGON2ID13 &&
		data[base_index + KDF_SENTINEL_OFFSET] == KDF_SENTINEL;
}

[[nodiscard]] std::uint64_t encryptCompressedFileToProfile(
	vBytes& profile_vec,
	int data_fd,
	std::size_t data_file_size,
	const std::string& data_filename,
	Option option,
	bool is_compressed_file,
	bool has_mastodon_option,
	std::size_t max_profile_size);

[[nodiscard]] std::optional<std::string> decryptDataFile(vBytes& png_vec, bool is_mastodon_file);
