#pragma once

#include "common.h"
#include "io_utils.h"

#include <array>
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

// KDF2: the key is Argon2id of the PIN's decimal digits (before pdvrdt 6.0).
// KDF3: the key is Argon2id of the 16 raw bytes of the 128-bit recovery KEY.
// Conceal writes only KDF3; both are recovered.
inline constexpr auto KDF_METADATA_MAGIC_V2 =
	std::to_array<Byte>({'K', 'D', 'F', '2'});
inline constexpr auto KDF_METADATA_MAGIC_V3 =
	std::to_array<Byte>({'K', 'D', 'F', '3'});

enum class KdfVersion : Byte { pin_v2, key_v3 };

inline constexpr auto PDVRDT_SIG =
	std::to_array<Byte>({0xC6, 0x50, 0x3C, 0xEA, 0x5E, 0x9D, 0xF9});

// Recovery never inflates an iCCP profile beyond this size. Conceal uses the
// same ceiling while building the pre-compression Mastodon profile so it can
// never emit an image that its own recovery path refuses to process.
inline constexpr std::size_t MAX_MASTODON_PROFILE_BYTES = 64uz * 1024 * 1024;

[[nodiscard]] inline std::optional<KdfVersion> kdfMetadataVersionAt(std::span<const Byte> data, std::size_t base_index) {
	if (base_index > data.size() || KDF_METADATA_REGION_BYTES > data.size() - base_index) {
		return std::nullopt;
	}
	if (data[base_index + KDF_ALG_OFFSET] != KDF_ALG_ARGON2ID13 ||
		data[base_index + KDF_SENTINEL_OFFSET] != KDF_SENTINEL) {
		return std::nullopt;
	}
	const Byte* const magic = data.data() + base_index + KDF_MAGIC_OFFSET;
	if (std::memcmp(magic, KDF_METADATA_MAGIC_V3.data(), KDF_METADATA_MAGIC_V3.size()) == 0) {
		return KdfVersion::key_v3;
	}
	if (std::memcmp(magic, KDF_METADATA_MAGIC_V2.data(), KDF_METADATA_MAGIC_V2.size()) == 0) {
		return KdfVersion::pin_v2;
	}
	return std::nullopt;
}

[[nodiscard]] inline bool hasSupportedKdfMetadataAt(std::span<const Byte> data, std::size_t base_index) {
	return kdfMetadataVersionAt(data, base_index).has_value();
}

// The word recovery asks for: the image says which kind of secret it takes.
[[nodiscard]] constexpr PinPrompt pinPromptFor(KdfVersion version) noexcept {
	return version == KdfVersion::key_v3 ? PinPrompt::key : PinPrompt::pin;
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

// Writes the freshly generated 128-bit recovery KEY into `out_pin` rather than
// returning it: a by-value return would leave one unwiped copy of the secret in
// the return slot until the caller re-wrapped it.
void encryptCompressedFileToProfile(
	RecoveryPin& out_pin,
	vBytes& profile_vec,
	int data_fd,
	std::size_t data_file_size,
	const std::string& data_filename,
	bool is_compressed_file,
	bool has_mastodon_option,
	std::size_t max_profile_size);

// Reddit has already compressed and capacity-checked its payload before PIN
// generation. Encrypt those prepared zlib bytes into the ordinary pdvrdt
// profile layout without running the compressor a second time.
void encryptPreparedDataToProfile(
	RecoveryPin& out_pin,
	vBytes& profile_vec,
	std::span<const Byte> prepared_data,
	const std::string& data_filename,
	bool has_mastodon_option,
	std::size_t max_profile_size);

// Exact size produced by encryptPreparedDataToProfile() for Reddit's default
// profile layout, including the fixed profile prefix, stream header, filename
// frame, payload frames and final frame. Used to reject/calculate capacity
// before spending an Argon2 pass.
[[nodiscard]] std::size_t computeRedditEncryptedProfileSize(
	std::size_t prepared_data_size,
	std::size_t filename_length);

// Prompt for the recovery KEY or PIN, using the word `prompt` names. Exposed
// because the Reddit carrier needs the secret *before* it can locate anything
// (see deriveCarrierKeysFromPin), so recover has to obtain it once and hand it
// to both steps rather than prompt a second time. Malformed entries and KEYs
// whose check characters do not match are rejected here, before any Argon2id.
void getPin(PinPrompt prompt, RecoveryPin& out_pin);

// Keys for the Reddit carrier's sample permutation and whitening, written into
// `out_keys` (like the KEY, never returned by value). Which keys depend on the
// kind of secret; each comes from a single Argon2id13 run (two passes, 64 MiB,
// 32-byte output), here called D:
//
// A 128-bit KEY (carrier version 4, which conceal writes):
//   D = Argon2id(the 16 KEY bytes, salt "pdvrdtcarrier-v4")
//   chacha = keyed BLAKE2b-256 of "pdvrdt carrier v4" under D.
// An old numeric PIN (recovery only):
//   D = Argon2id(the PIN as 8 big-endian bytes, salt "pdvrdtcarrier-v2")
//   chacha = keyed BLAKE2b-256 of "pdvrdt carrier v3" under D (version 3);
//   v2 = the first eight bytes of D, read big-endian (splitmix64, pdvrdt 5.1).
//
// One derivation serves both old versions, so recovering a version-2 image
// after the version-3 attempt misses costs no second Argon2 pass. The fixed
// salts are available before the encrypted payload's random salt can be
// located; they let derivation work be reused across images, but every
// candidate still costs an Argon2 evaluation, and a 128-bit KEY cannot be
// searched. These parameters are part of the carrier wire format and must not
// change without a new carrier version. Payload confidentiality comes from
// secretstream under its own salted Argon2 key, not from this.
struct CarrierKeys {
	static constexpr std::size_t CHACHA_KEY_BYTES = 32;

	std::array<Byte, CHACHA_KEY_BYTES> chacha{};
	Byte chacha_version{};   // carrier header version `chacha` keys: 4 or 3
	bool has_v2{false};
	SensitiveU64 v2{};

	CarrierKeys() = default;
	CarrierKeys(const CarrierKeys&) = delete;
	CarrierKeys& operator=(const CarrierKeys&) = delete;
	~CarrierKeys() { sodium_memzero(chacha.data(), chacha.size()); }
};

void deriveCarrierKeysFromPin(const RecoveryPin& pin, CarrierKeys& out_keys);

// Recovery-only compatibility with version-1 carriers from pdvrdt 5.0.
// Its fast PIN hash must never be used to conceal a new payload. Old numeric
// PINs only.
void deriveLegacyCarrierKeyFromPin(const RecoveryPin& pin, SensitiveU64& out_key);

// Decrypt the embedded profile in place with a secret the caller has collected
// (see getPin). nullopt on a wrong KEY/PIN, a secret of the wrong kind for the
// image, or failed authentication.
[[nodiscard]] std::optional<std::string> decryptDataFileWithPin(
	vBytes& png_vec,
	const RecoveryPin& recovery_pin,
	bool is_mastodon_file);
