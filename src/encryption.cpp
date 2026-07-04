#include "encryption.h"
#include "compression.h"
#include "io_utils.h"
#include "png_utils.h"

#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <format>
#include <limits>
#include <print>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>

namespace {
struct TermiosGuard {
	termios old{};
	bool active{false};

	TermiosGuard(const TermiosGuard&) = delete;
	TermiosGuard& operator=(const TermiosGuard&) = delete;

	TermiosGuard() {
		if (!isatty(STDIN_FILENO)) {
			return;
		}
		if (tcgetattr(STDIN_FILENO, &old) != 0) {
			return;
		}
		termios newt = old;
		const auto mask = static_cast<tcflag_t>(ICANON | ECHO);
		newt.c_lflag &= static_cast<tcflag_t>(~mask);
		if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &newt) == 0) {
			active = true;
		}
	}

	~TermiosGuard() {
		if (active) {
			tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
		}
	}
};

struct SensitiveKeyData {
	Key key{};

	~SensitiveKeyData() {
		sodium_memzero(key.data(), key.size());
	}

	SensitiveKeyData(const SensitiveKeyData&) = delete;
	SensitiveKeyData& operator=(const SensitiveKeyData&) = delete;
	SensitiveKeyData() = default;
};

struct SensitiveBytesGuard {
	vBytes* bytes{};
	bool active{true};

	SensitiveBytesGuard(const SensitiveBytesGuard&) = delete;
	SensitiveBytesGuard& operator=(const SensitiveBytesGuard&) = delete;

	explicit SensitiveBytesGuard(vBytes& in_bytes)
		: bytes(&in_bytes) {}

	~SensitiveBytesGuard() {
		if (active && bytes != nullptr && !bytes->empty()) {
			sodium_memzero(bytes->data(), bytes->size());
		}
	}

	void release() noexcept {
		active = false;
	}
};

struct SecretStreamStateGuard {
	crypto_secretstream_xchacha20poly1305_state state{};

	SecretStreamStateGuard(const SecretStreamStateGuard&) = delete;
	SecretStreamStateGuard& operator=(const SecretStreamStateGuard&) = delete;
	SecretStreamStateGuard() = default;

	~SecretStreamStateGuard() {
		sodium_memzero(&state, sizeof(state));
	}
};

constexpr std::size_t
	STREAM_CHUNK_SIZE      = 1 * 1024 * 1024,
	STREAM_FRAME_LEN_BYTES = 4;

using StreamHeader = std::array<Byte, crypto_secretstream_xchacha20poly1305_HEADERBYTES>;

struct KdfSecrets {
	Salt salt{};
	StreamHeader stream_header{};
};

[[nodiscard]] std::array<Byte, STREAM_FRAME_LEN_BYTES> encodeFrameLen(std::uint32_t frame_len) {
	std::array<Byte, STREAM_FRAME_LEN_BYTES> bytes{};
	updateValue(bytes, 0, frame_len, STREAM_FRAME_LEN_BYTES);
	return bytes;
}

void appendEncryptedFrame(vBytes& encrypted, std::span<const Byte> cipher_frame) {
	if (cipher_frame.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
		throw std::runtime_error("crypto_secretstream frame too large.");
	}

	const auto frame_len = encodeFrameLen(static_cast<std::uint32_t>(cipher_frame.size()));
	const std::size_t base = encrypted.size();
	const std::size_t final_size = checkedAddSize(
		checkedAddSize(base, frame_len.size(), "File Size Error: Encrypted output overflow."),
		cipher_frame.size(),
		"File Size Error: Encrypted output overflow."
	);

	encrypted.resize(final_size);
	Byte* out = encrypted.data() + static_cast<std::ptrdiff_t>(base);
	std::memcpy(out, frame_len.data(), frame_len.size());
	std::memcpy(out + frame_len.size(), cipher_frame.data(), cipher_frame.size());
}

void initializeSecretStreamPush(
	SecretStreamStateGuard& stream_state,
	StreamHeader& stream_header,
	const Key& key) {

	if (crypto_secretstream_xchacha20poly1305_init_push(&stream_state.state, stream_header.data(), key.data()) != 0) {
		throw std::runtime_error("crypto_secretstream init_push failed.");
	}
}

void appendEncryptedFrames(
	vBytes& encrypted,
	std::span<const Byte> plaintext,
	crypto_secretstream_xchacha20poly1305_state& state,
	vBytes& cipher_chunk,
	unsigned char final_tag = 0) {

	if (plaintext.empty() && final_tag == 0) {
		return;
	}

	std::size_t offset = 0;
	bool emit_empty_final = plaintext.empty() && final_tag != 0;

	while (emit_empty_final || offset < plaintext.size()) {
		const std::size_t remaining = plaintext.size() - offset;
		const std::size_t chunk_len = std::min(remaining, STREAM_CHUNK_SIZE);
		const bool is_final = emit_empty_final || (offset + chunk_len == plaintext.size());
		const unsigned char tag = is_final ? final_tag : 0;

		unsigned long long clen_ull = 0;
		if (crypto_secretstream_xchacha20poly1305_push(
				&state,
				cipher_chunk.data(),
				&clen_ull,
				(chunk_len == 0) ? nullptr : (plaintext.data() + static_cast<std::ptrdiff_t>(offset)),
				chunk_len,
				nullptr,
				0,
				tag) != 0) {
			throw std::runtime_error("crypto_secretstream push failed.");
		}

		if (clen_ull > static_cast<unsigned long long>(std::numeric_limits<std::uint32_t>::max())) {
			throw std::runtime_error("crypto_secretstream frame too large.");
		}
		appendEncryptedFrame(
			encrypted,
			std::span<const Byte>(cipher_chunk.data(), static_cast<std::size_t>(clen_ull))
		);

		offset += chunk_len;
		emit_empty_final = false;
	}
}

[[nodiscard]] std::uint32_t readFrameLen(std::span<const Byte> data, std::size_t index) {
	return static_cast<std::uint32_t>(getValue(data, index, STREAM_FRAME_LEN_BYTES));
}

// Decrypt in place: cipher at storage[cipher_start..cipher_start+cipher_len),
// plaintext written to storage[0..]. Plaintext write trails unread cipher by at least
// (cipher_start + header.size() + 21*N) bytes after frame N — always safe.
// On success, storage is resized to plaintext size. On failure, any partial plaintext is wiped.
[[nodiscard]] bool decryptWithSecretStreamInPlace(
	vBytes& storage,
	std::size_t cipher_start,
	std::size_t cipher_len,
	const Key& key,
	const StreamHeader& header) {

	if (cipher_len < header.size()) {
		return false;
	}
	if (!spanHasRange(storage, cipher_start, cipher_len)) {
		return false;
	}
	if (!std::ranges::equal(
			std::span<const Byte>(storage.data() + cipher_start, header.size()),
			header)) {
		return false;
	}

	SecretStreamStateGuard stream_state;
	if (crypto_secretstream_xchacha20poly1305_init_pull(&stream_state.state, header.data(), key.data()) != 0) {
		return false;
	}

	vBytes plain_chunk(STREAM_CHUNK_SIZE);
	SensitiveBytesGuard plain_chunk_guard(plain_chunk);

	std::size_t cipher_pos = cipher_start + header.size();
	const std::size_t cipher_end = cipher_start + cipher_len;
	std::size_t plain_pos = 0;
	bool has_final_tag = false;

	auto fail = [&]() -> bool {
		sodium_memzero(storage.data(), plain_pos);
		return false;
	};

	while (cipher_pos < cipher_end) {
		if (cipher_end - cipher_pos < STREAM_FRAME_LEN_BYTES) {
			return fail();
		}

		const std::uint32_t frame_len = readFrameLen(storage, cipher_pos);
		cipher_pos += STREAM_FRAME_LEN_BYTES;

		if (frame_len < crypto_secretstream_xchacha20poly1305_ABYTES ||
			frame_len > cipher_end - cipher_pos) {
			return fail();
		}

		const std::size_t max_plain_chunk = static_cast<std::size_t>(frame_len) - crypto_secretstream_xchacha20poly1305_ABYTES;
		if (max_plain_chunk > plain_chunk.size()) {
			return fail();
		}

		unsigned long long mlen = 0;
		unsigned char tag = 0;
		if (crypto_secretstream_xchacha20poly1305_pull(
				&stream_state.state,
				plain_chunk.data(),
				&mlen,
				&tag,
				storage.data() + cipher_pos,
				frame_len,
				nullptr,
				0) != 0) {
			return fail();
		}
		if (mlen > max_plain_chunk) {
			return fail();
		}
		if (mlen > 0) {
			std::memcpy(storage.data() + plain_pos, plain_chunk.data(), mlen);
			plain_pos += mlen;
		}

		cipher_pos += frame_len;
		if (tag == crypto_secretstream_xchacha20poly1305_TAG_FINAL) {
			has_final_tag = true;
			break;
		}
	}

	if (!has_final_tag || cipher_pos != cipher_end) {
		return fail();
	}

	// Wipe trailing ciphertext bytes before truncating.
	if (plain_pos < storage.size()) {
		sodium_memzero(storage.data() + plain_pos, storage.size() - plain_pos);
	}
	storage.resize(plain_pos);
	return true;
}

void deriveKeyFromPin(Key& out_key, std::uint64_t pin, const Salt& salt) {
	std::array<char, 32> pin_buf{};
	auto [ptr, ec] = std::to_chars(pin_buf.data(), pin_buf.data() + pin_buf.size(), pin);
	if (ec != std::errc{}) {
		sodium_memzero(pin_buf.data(), pin_buf.size());
		sodium_memzero(&pin, sizeof(pin));
		throw std::runtime_error("KDF Error: Failed to encode recovery PIN.");
	}

	const auto pin_len = static_cast<unsigned long long>(ptr - pin_buf.data());
	const int rc = crypto_pwhash(
		out_key.data(),
		out_key.size(),
		pin_buf.data(),
		pin_len,
		salt.data(),
		crypto_pwhash_OPSLIMIT_INTERACTIVE,
		crypto_pwhash_MEMLIMIT_INTERACTIVE,
		crypto_pwhash_ALG_ARGON2ID13
	);

	sodium_memzero(pin_buf.data(), pin_buf.size());
	sodium_memzero(&pin, sizeof(pin));

	if (rc != 0) {
		throw std::runtime_error("KDF Error: Unable to derive encryption key.");
	}
}

[[nodiscard]] std::uint64_t generateRecoveryPin() {
	std::uint64_t pin = 0;
	while (pin == 0) {
		randombytes_buf(&pin, sizeof(pin));
	}
	return pin;
}

inline constexpr std::size_t MAX_FILENAME_PREFIX_BYTES =
	static_cast<std::size_t>(std::numeric_limits<Byte>::max()) + 1;

struct FilenamePrefix {
	std::array<Byte, MAX_FILENAME_PREFIX_BYTES> bytes{};
	std::size_t size{};

	[[nodiscard]] std::span<const Byte> view() const noexcept {
		return std::span<const Byte>(bytes.data(), size);
	}
};

[[nodiscard]] FilenamePrefix makeFilenamePrefix(const std::string& data_filename) {
	if (data_filename.empty() || data_filename.size() > static_cast<std::size_t>(std::numeric_limits<Byte>::max())) {
		throw std::runtime_error("Data File Error: Invalid data filename length.");
	}

	FilenamePrefix filename_prefix;
	filename_prefix.size = 1 + data_filename.size();
	filename_prefix.bytes[0] = static_cast<Byte>(data_filename.size());
	std::memcpy(
		filename_prefix.bytes.data() + 1,
		data_filename.data(),
		data_filename.size()
	);
	return filename_prefix;
}

void writeKdfMetadata(
	vBytes& profile_vec,
	const ProfileOffsets& offsets,
	const Salt& salt,
	const StreamHeader& stream_header,
	std::string_view corrupt_profile_error) {

	randombytes_buf(profile_vec.data() + offsets.kdf_metadata, KDF_METADATA_REGION_BYTES);
	std::ranges::copy(
		KDF_METADATA_MAGIC_V2,
		profile_vec.begin() + static_cast<std::ptrdiff_t>(offsets.kdf_metadata + KDF_MAGIC_OFFSET)
	);
	profile_vec[offsets.kdf_metadata + KDF_ALG_OFFSET] = KDF_ALG_ARGON2ID13;
	profile_vec[offsets.kdf_metadata + KDF_SENTINEL_OFFSET] = KDF_SENTINEL;

	requireSpanRange(profile_vec, offsets.kdf_metadata + KDF_SALT_OFFSET, salt.size(), corrupt_profile_error);
	requireSpanRange(profile_vec, offsets.kdf_metadata + KDF_NONCE_OFFSET, stream_header.size(), corrupt_profile_error);
	std::ranges::copy(salt, profile_vec.begin() + static_cast<std::ptrdiff_t>(offsets.kdf_metadata + KDF_SALT_OFFSET));
	std::ranges::copy(stream_header, profile_vec.begin() + static_cast<std::ptrdiff_t>(offsets.kdf_metadata + KDF_NONCE_OFFSET));
}

[[nodiscard]] KdfSecrets readKdfSecrets(
	std::span<const Byte> data,
	const ProfileOffsets& offsets,
	std::string_view corrupt_file_error) {

	KdfSecrets secrets;
	requireSpanRange(data, offsets.kdf_metadata + KDF_SALT_OFFSET, secrets.salt.size(), corrupt_file_error);
	requireSpanRange(data, offsets.kdf_metadata + KDF_NONCE_OFFSET, secrets.stream_header.size(), corrupt_file_error);

	std::ranges::copy_n(
		data.begin() + static_cast<std::ptrdiff_t>(offsets.kdf_metadata + KDF_SALT_OFFSET),
		static_cast<std::ptrdiff_t>(secrets.salt.size()),
		secrets.salt.begin()
	);
	std::ranges::copy_n(
		data.begin() + static_cast<std::ptrdiff_t>(offsets.kdf_metadata + KDF_NONCE_OFFSET),
		static_cast<std::ptrdiff_t>(secrets.stream_header.size()),
		secrets.stream_header.begin()
	);
	return secrets;
}

[[nodiscard]] constexpr std::size_t minimumStreamCipherSize() {
	return crypto_secretstream_xchacha20poly1305_HEADERBYTES +
		STREAM_FRAME_LEN_BYTES +
		crypto_secretstream_xchacha20poly1305_ABYTES;
}

[[nodiscard]] std::string extractFilenamePrefix(vBytes& payload) {
	constexpr const char* CORRUPT_FILE_ERROR = "File Recovery Error: Embedded profile is corrupt.";
	if (payload.empty()) {
		throw std::runtime_error(CORRUPT_FILE_ERROR);
	}

	const std::size_t filename_len = payload[0];
	if (filename_len == 0) {
		throw std::runtime_error(CORRUPT_FILE_ERROR);
	}
	const std::size_t prefix_len = 1 + filename_len;
	requireSpanRange(payload, 0, prefix_len, CORRUPT_FILE_ERROR);

	std::string decrypted_filename(
		reinterpret_cast<const char*>(payload.data() + 1),
		filename_len
	);

	const std::size_t old_payload_size = payload.size();
	const std::size_t compressed_payload_size = old_payload_size - prefix_len;
	if (compressed_payload_size > 0) {
		std::memmove(
			payload.data(),
			payload.data() + static_cast<std::ptrdiff_t>(prefix_len),
			compressed_payload_size
		);
		sodium_memzero(
			payload.data() + static_cast<std::ptrdiff_t>(compressed_payload_size),
			prefix_len
		);
	} else {
		sodium_memzero(payload.data(), old_payload_size);
	}
	payload.resize(compressed_payload_size);
	return decrypted_filename;
}

[[nodiscard]] std::uint64_t getPin() {
	constexpr auto MAX_UINT64_STR = std::string_view{"18446744073709551615"};
	constexpr std::size_t MAX_PIN_LENGTH = 20;

	std::print("\nPIN: ");
	std::fflush(stdout);

	std::array<char, MAX_PIN_LENGTH> input{};
	std::size_t input_len = 0;
	char ch{};
	const bool is_tty = (isatty(STDIN_FILENO) != 0);
	TermiosGuard termios_guard;
	bool input_overflow = false;
	bool invalid_input = false;
	bool read_error = false;

	while (true) {
		const ssize_t bytes_read = read(STDIN_FILENO, &ch, 1);
		if (bytes_read == 0) break;
		if (bytes_read < 0) {
			if (errno == EINTR) continue;
			read_error = true;
			break;
		}
		if (ch == '\n' || ch == '\r') break;
		if (ch >= '0' && ch <= '9') {
			if (input_len >= input.size()) {
				input_overflow = true;
				continue;
			}
			input[input_len++] = ch;
			if (is_tty) {
				std::print("*");
				std::fflush(stdout);
			}
		} else if ((ch == '\b' || ch == 127) && input_len != 0) {
			if (is_tty) {
				std::print("\b \b");
				std::fflush(stdout);
			}
			input[--input_len] = '\0';
			input_overflow = false;
		} else {
			invalid_input = true;
		}
	}

	std::println("");
	std::fflush(stdout);

	auto wipe_input = [&]() {
		sodium_memzero(input.data(), input.size());
		input_len = 0;
	};

	const std::string_view input_view(input.data(), input_len);
	if (read_error ||
		invalid_input ||
		input_overflow ||
		input_view.empty() ||
		(input_view.length() == MAX_PIN_LENGTH && input_view > MAX_UINT64_STR)) {
		wipe_input();
		return 0;
	}

	std::uint64_t result = 0;
	auto [ptr, ec] = std::from_chars(input.data(), input.data() + input_len, result);
	if (ec != std::errc{} || ptr != input.data() + input_len) {
		wipe_input();
		return 0;
	}

	wipe_input();
	return result;
}

} // namespace

std::uint64_t encryptCompressedFileToProfile(
	vBytes& profile_vec,
	const fs::path& data_file_path,
	const std::string& data_filename,
	Option option,
	bool is_compressed_file,
	bool has_mastodon_option) {

	const auto& offsets = has_mastodon_option ? MASTODON_OFFSETS : DEFAULT_OFFSETS;
	constexpr const char* CORRUPT_PROFILE_ERROR = "Internal Error: Corrupt profile template.";

	requireSpanRange(profile_vec, offsets.kdf_metadata, KDF_METADATA_REGION_BYTES, CORRUPT_PROFILE_ERROR);
	if (offsets.encrypted_file != profile_vec.size()) {
		throw std::runtime_error(CORRUPT_PROFILE_ERROR);
	}

	const FilenamePrefix filename_prefix = makeFilenamePrefix(data_filename);

	SensitiveKeyData key;
	Salt salt{};
	StreamHeader stream_header{};
	const std::uint64_t pin = generateRecoveryPin();

	randombytes_buf(salt.data(), salt.size());
	deriveKeyFromPin(key.key, pin, salt);

	SecretStreamStateGuard stream_state;
	initializeSecretStreamPush(stream_state, stream_header, key.key);

	appendBytes(profile_vec, std::span<const Byte>(stream_header), "File Size Error: Encrypted output overflow.");

	vBytes cipher_chunk(STREAM_CHUNK_SIZE + crypto_secretstream_xchacha20poly1305_ABYTES);

	appendEncryptedFrames(profile_vec, filename_prefix.view(), stream_state.state, cipher_chunk);

	bool saw_compressed_output = false;
	zlibDeflateFile(data_file_path, option, is_compressed_file, [&](std::span<const Byte> chunk) {
		if (chunk.empty()) {
			return;
		}
		saw_compressed_output = true;
		appendEncryptedFrames(profile_vec, chunk, stream_state.state, cipher_chunk);
	});

	if (!saw_compressed_output) {
		throw std::runtime_error("File Size Error: File is zero bytes. Probable compression failure.");
	}

	// Close the secretstream with an empty TAG_FINAL frame (~21 bytes overhead).
	appendEncryptedFrames(
		profile_vec,
		std::span<const Byte>{},
		stream_state.state,
		cipher_chunk,
		crypto_secretstream_xchacha20poly1305_TAG_FINAL
	);

	writeKdfMetadata(profile_vec, offsets, salt, stream_header, CORRUPT_PROFILE_ERROR);
	return pin;
}

std::optional<std::string> decryptDataFile(vBytes& png_vec, bool is_mastodon_file) {
	const auto& offsets = is_mastodon_file ? MASTODON_OFFSETS : DEFAULT_OFFSETS;

	constexpr const char* CORRUPT_FILE_ERROR = "File Recovery Error: Embedded profile is corrupt.";
	requireSpanRange(png_vec, offsets.kdf_metadata, KDF_METADATA_REGION_BYTES, CORRUPT_FILE_ERROR);
	if (offsets.encrypted_file > png_vec.size()) {
		throw std::runtime_error(CORRUPT_FILE_ERROR);
	}

	if (!hasSupportedKdfMetadataAt(png_vec, offsets.kdf_metadata)) {
		throw std::runtime_error(
			"File Decryption Error: Unsupported legacy encrypted file format. "
			"Use an older pdvrdt release to recover this file.");
	}

	std::uint64_t recovery_pin = getPin();

	SensitiveKeyData key;
	const KdfSecrets secrets = readKdfSecrets(png_vec, offsets, CORRUPT_FILE_ERROR);
	deriveKeyFromPin(key.key, recovery_pin, secrets.salt);
	sodium_memzero(&recovery_pin, sizeof(recovery_pin));

	const std::size_t ciphertext_length = png_vec.size() - offsets.encrypted_file;
	if (ciphertext_length < minimumStreamCipherSize()) {
		throw std::runtime_error(CORRUPT_FILE_ERROR);
	}

	if (!decryptWithSecretStreamInPlace(png_vec, offsets.encrypted_file, ciphertext_length, key.key, secrets.stream_header)) {
		return std::nullopt;
	}

	return extractFilenamePrefix(png_vec);
}
