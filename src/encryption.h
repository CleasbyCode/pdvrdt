#pragma once

#include "common.h"
#include "io_utils.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <functional>
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

// KDF3: the key is Argon2id of the 16 raw bytes of the 128-bit recovery KEY.
// It is the only format conceal writes and recover decrypts. KDF2 (a numeric
// PIN, before pdvrdt 6.0) is still recognised, but only so that such an image
// is reported as too old instead of being asked for a KEY, and so that conceal
// still strips its payload from a reused cover.
inline constexpr auto KDF_METADATA_MAGIC_V2 =
	std::to_array<Byte>({'K', 'D', 'F', '2'});
inline constexpr auto KDF_METADATA_MAGIC_V3 =
	std::to_array<Byte>({'K', 'D', 'F', '3'});

enum class KdfVersion : Byte { pre_v6_pin, key_v3 };

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
		return KdfVersion::pre_v6_pin;
	}
	return std::nullopt;
}

[[nodiscard]] inline bool hasSupportedKdfMetadataAt(std::span<const Byte> data, std::size_t base_index) {
	return kdfMetadataVersionAt(data, base_index).has_value();
}

// Reported for a payload this build recognises but does not decrypt.
inline constexpr const char* UNSUPPORTED_FORMAT_ERROR =
	"File Decryption Error: Unsupported encrypted file format. "
	"Images made before pdvrdt 6.0 need the legacy pdvrdt release.";

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

// Writes the freshly generated 128-bit recovery KEY into `out_recovery_key` rather than
// returning it: a by-value return would leave one unwiped copy of the secret in
// the return slot until the caller re-wrapped it. Returns the size of the
// zlib stream that was encrypted (deflated, or stored for compressed input).
[[nodiscard]] std::size_t encryptCompressedFileToProfile(
	RecoveryKey& out_recovery_key,
	vBytes& profile_vec,
	int data_fd,
	std::size_t data_file_size,
	const std::string& data_filename,
	bool is_compressed_file,
	bool has_mastodon_option,
	std::size_t max_profile_size);

// Receives an encrypted profile piece by piece, in order.
using ProfileSink = std::function<void(std::span<const Byte>)>;

// encryptCompressedFileToProfile() for the default layout, without holding the
// profile: `profile_template` (the default template; its KDF metadata is
// filled in) and then the ciphertext go to `sink` as they are produced, so
// memory stays bounded however large the payload. The profile's size is the
// total handed to the sink. Returns the size of the zlib stream encrypted.
[[nodiscard]] std::size_t encryptCompressedFileToSink(
	RecoveryKey& out_recovery_key,
	vBytes& profile_template,
	int data_fd,
	std::size_t data_file_size,
	const std::string& data_filename,
	bool is_compressed_file,
	std::size_t max_profile_size,
	const ProfileSink& sink);

// Reddit has already compressed and capacity-checked its payload before KEY
// generation. Encrypt those prepared zlib bytes into the ordinary pdvrdt
// profile layout without running the compressor a second time.
void encryptPreparedDataToProfile(
	RecoveryKey& out_recovery_key,
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

// Prompt for the recovery KEY. Exposed because the Reddit carrier needs the
// KEY *before* it can locate anything (see deriveCarrierKeyFromRecoveryKey), so recover
// has to obtain it once and hand it to both steps rather than prompt a second
// time. Malformed entries and KEYs whose check characters do not match are
// rejected here, before any Argon2id.
void getRecoveryKey(RecoveryKey& out_recovery_key);

// The key for the Reddit carrier's sample permutation and whitening (carrier
// version 4), written into `out_key` (like the KEY, never returned by value):
//
//   D = Argon2id13(the 16 KEY bytes, salt "pdvrdtcarrier-v4",
//                  two passes, 64 MiB, 32-byte output)
//   key = keyed BLAKE2b-256 of "pdvrdt carrier v4" under D.
//
// The fixed salt is available before the encrypted payload's random salt can
// be located; it lets derivation work be reused across images, but every
// candidate still costs an Argon2 evaluation, and a 128-bit KEY cannot be
// searched. These parameters are part of the carrier wire format and must not
// change without a new carrier version. Payload confidentiality comes from
// secretstream under its own salted Argon2 key, not from this.
struct CarrierKey {
	static constexpr std::size_t BYTES = 32;

	std::array<Byte, BYTES> bytes{};

	CarrierKey() = default;
	CarrierKey(const CarrierKey&) = delete;
	CarrierKey& operator=(const CarrierKey&) = delete;
	~CarrierKey() { sodium_memzero(bytes.data(), bytes.size()); }
};

void deriveCarrierKeyFromRecoveryKey(const RecoveryKey& recovery_key, CarrierKey& out_key);

// Decrypt the embedded profile in place with a secret the caller has collected
// (see getRecoveryKey). nullopt on a wrong KEY or failed authentication; a payload
// made before pdvrdt 6.0 throws UNSUPPORTED_FORMAT_ERROR. `on_key_derived` runs
// once the Argon2id key is ready, before anything is decrypted, so recover can
// report the two steps apart; the streaming form below takes it too.
[[nodiscard]] std::optional<std::string> decryptDataFileWithKey(
	vBytes& png_vec,
	const RecoveryKey& recovery_key,
	bool is_mastodon_file,
	const std::function<void()>& on_key_derived);

// The streaming form of decryptDataFileWithKey() for a default-layout profile
// held in a file, so a payload of any size is never in memory whole.
// `profile_head` is the profile's first DEFAULT_OFFSETS.encrypted_file bytes
// (KDF metadata and signature); the `ciphertext_length` bytes after them are
// read strictly in order, through `read_ciphertext`, which must fill the span
// it is given. The plaintext of each frame is handed to `on_plaintext` once
// that frame has authenticated. Returns false on a wrong KEY or a failed
// authentication -- then everything already handed over must be discarded --
// and throws UNSUPPORTED_FORMAT_ERROR or a corruption error as the in-memory
// form does.
[[nodiscard]] bool decryptStreamWithKey(
	std::span<const Byte> profile_head,
	std::size_t ciphertext_length,
	const RecoveryKey& recovery_key,
	const std::function<void()>& on_key_derived,
	const std::function<void(std::span<Byte>)>& read_ciphertext,
	const std::function<void(std::span<const Byte>)>& on_plaintext);
