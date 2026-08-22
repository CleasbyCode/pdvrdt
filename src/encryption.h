#pragma once

#include "common.h"
#include "io_utils.h"

#include <cstddef>
#include <cstring>
#include <optional>
#include <span>

struct ProfileOffsets {
	std::size_t
		kdf_metadata,   // 56-byte metadata region (magic/alg/salt/nonce/random padding)
		encrypted_file,
		pdv_signature;  // PDVRDT_SIG, the second half of the payload fingerprint
};

inline constexpr ProfileOffsets
	MASTODON_OFFSETS = { 0x1BE, 0x1FE, 502 },
	DEFAULT_OFFSETS  = { 0x02D, 0x06E, 101 };

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

// Wire format: each crypto_secretstream frame is preceded by its big-endian
// length in this many bytes. Part of the on-disk layout, so it lives here beside
// the offset tables rather than inside encryption.cpp.
inline constexpr std::size_t STREAM_FRAME_LEN_BYTES = 4;

// Smallest ciphertext a well-formed payload can have: the stream header plus one
// framed TAG_FINAL frame. Used by conceal/recover to reject truncated payloads.
[[nodiscard]] inline constexpr std::size_t minimumStreamCipherSize() {
	return crypto_secretstream_xchacha20poly1305_HEADERBYTES +
		STREAM_FRAME_LEN_BYTES +
		crypto_secretstream_xchacha20poly1305_ABYTES;
}

inline constexpr auto KDF_METADATA_MAGIC_V2 =
	std::to_array<Byte>({'K', 'D', 'F', '2'});

inline constexpr auto PDVRDT_SIG =
	std::to_array<Byte>({0xC6, 0x50, 0x3C, 0xEA, 0x5E, 0x9D, 0xF9});

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

// The two fixed byte strings that introduce an embedded payload: a fake zlib
// header for the default IDAT layout, and "icc\0" + compression method 0 for the
// Mastodon iCCP layout.
inline constexpr auto PDVRDT_IDAT_PREFIX = std::to_array<Byte>({ 0x78, 0x5E, 0x5C });
inline constexpr auto PDVRDT_ICCP_PREFIX = std::to_array<Byte>({ 0x69, 0x63, 0x63, 0x00, 0x00 });

// True if `profile` carries pdvrdt's KDF metadata and signature at the offsets
// for this layout -- the payload fingerprint.
//
// Deliberately liberal: conceal uses it to strip a stale payload out of a reused
// cover, so it must still match one too truncated for recovery to accept. The
// recover side adds its own length requirement on top.
[[nodiscard]] inline bool hasPdvrdtProfileMarkers(std::span<const Byte> profile, const ProfileOffsets& offsets) {
	return hasSupportedKdfMetadataAt(profile, offsets.kdf_metadata) &&
		bytesEqualAt(profile, offsets.pdv_signature, PDVRDT_SIG);
}

// The zlib-compressed profile inside an iCCP chunk if that chunk is a pdvrdt
// Mastodon payload, or nullopt if it is an ordinary ICC profile.
//
// Inflates only the fixed metadata/signature prefix, so a genuine profile or a
// decompression bomb is rejected without expanding it. Single source of truth:
// conceal must strip such a payload when its image is reused as a cover and
// recover must extract it, and the two can never be allowed to disagree about
// which chunks count.
[[nodiscard]] std::optional<std::span<const Byte>> findPdvrdtIccpPayload(std::span<const Byte> iccp_data);

// Writes the freshly generated recovery PIN into `out_pin` rather than
// returning it: a by-value return would leave one unwiped copy of the secret in
// the return slot until the caller re-wrapped it.
void encryptCompressedFileToProfile(
	SensitiveU64& out_pin,
	vBytes& profile_vec,
	int data_fd,
	std::size_t data_file_size,
	const std::string& data_filename,
	bool is_compressed_file,
	bool has_mastodon_option,
	std::size_t max_profile_size);

[[nodiscard]] std::optional<std::string> decryptDataFile(vBytes& png_vec, bool is_mastodon_file);
