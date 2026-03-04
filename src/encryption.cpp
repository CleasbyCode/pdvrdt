#include "encryption.h"
#include "png_utils.h"

#include <cerrno>

namespace {
constexpr auto KDF_METADATA_MAGIC_V2 = std::to_array<Byte>({'K', 'D', 'F', '2'});

[[nodiscard]] bool spanHasRange(std::span<const Byte> data, std::size_t index, std::size_t length) {
	return index <= data.size() && length <= data.size() - index;
}

void requireSpanRange(std::span<const Byte> data, std::size_t index, std::size_t length, std::string_view error_message) {
	if (!spanHasRange(data, index, length)) {
		throw std::runtime_error(std::string(error_message));
	}
}

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
		if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) == 0) {
			active = true;
		}
	}

	~TermiosGuard() {
		if (active) {
			tcsetattr(STDIN_FILENO, TCSANOW, &old);
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

constexpr std::size_t
	STREAM_CHUNK_SIZE      = 1 * 1024 * 1024,
	STREAM_FRAME_LEN_BYTES = 4;

using StreamHeader = std::array<Byte, crypto_secretstream_xchacha20poly1305_HEADERBYTES>;

[[nodiscard]] std::size_t computeStreamEncryptedSize(std::size_t plaintext_size) {
	const std::size_t chunks = (plaintext_size == 0)
		? 1
		: ((plaintext_size - 1) / STREAM_CHUNK_SIZE) + 1;
	const std::size_t per_chunk_overhead = STREAM_FRAME_LEN_BYTES + crypto_secretstream_xchacha20poly1305_ABYTES;

	if (chunks > (std::numeric_limits<std::size_t>::max() - StreamHeader{}.size() - plaintext_size) / per_chunk_overhead) {
		throw std::runtime_error("Data File Error: Data file too large to encrypt.");
	}

	return StreamHeader{}.size() + plaintext_size + (chunks * per_chunk_overhead);
}

void writeFrameLen(vBytes& out, std::uint32_t frame_len) {
	out.push_back(static_cast<Byte>((frame_len >> 24) & 0xFF));
	out.push_back(static_cast<Byte>((frame_len >> 16) & 0xFF));
	out.push_back(static_cast<Byte>((frame_len >> 8) & 0xFF));
	out.push_back(static_cast<Byte>(frame_len & 0xFF));
}

[[nodiscard]] std::uint32_t readFrameLen(std::span<const Byte> data, std::size_t index) {
	return (static_cast<std::uint32_t>(data[index]) << 24) |
		   (static_cast<std::uint32_t>(data[index + 1]) << 16) |
		   (static_cast<std::uint32_t>(data[index + 2]) << 8) |
		    static_cast<std::uint32_t>(data[index + 3]);
}

void encryptWithSecretStream(vBytes& plaintext, const Key& key, StreamHeader& header) {
	crypto_secretstream_xchacha20poly1305_state state{};
	if (crypto_secretstream_xchacha20poly1305_init_push(&state, header.data(), key.data()) != 0) {
		throw std::runtime_error("crypto_secretstream init_push failed.");
	}

	vBytes encrypted;
	encrypted.reserve(computeStreamEncryptedSize(plaintext.size()));
	encrypted.insert(encrypted.end(), header.begin(), header.end());
	vBytes cipher_chunk(STREAM_CHUNK_SIZE + crypto_secretstream_xchacha20poly1305_ABYTES);

	std::size_t offset = 0;
	bool emitted_final = false;

	while (!emitted_final) {
		const std::size_t remaining = (offset <= plaintext.size()) ? (plaintext.size() - offset) : 0;
		const std::size_t chunk_len = std::min(remaining, STREAM_CHUNK_SIZE);
		const bool is_final = (offset + chunk_len == plaintext.size());
		const unsigned char tag = is_final
			? crypto_secretstream_xchacha20poly1305_TAG_FINAL
			: 0;

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
		const auto clen = static_cast<std::uint32_t>(clen_ull);
		writeFrameLen(encrypted, clen);
		encrypted.insert(
			encrypted.end(),
			cipher_chunk.begin(),
			cipher_chunk.begin() + static_cast<std::ptrdiff_t>(clen)
		);

		offset += chunk_len;
		emitted_final = is_final;
	}

	if (!plaintext.empty()) {
		sodium_memzero(plaintext.data(), plaintext.size());
	}

	plaintext = std::move(encrypted);
}

[[nodiscard]] bool decryptWithSecretStream(
	vBytes& framed_ciphertext,
	const Key& key,
	const StreamHeader& header) {

	crypto_secretstream_xchacha20poly1305_state state{};
	if (crypto_secretstream_xchacha20poly1305_init_pull(&state, header.data(), key.data()) != 0) {
		return false;
	}

	vBytes decrypted;
	decrypted.reserve(framed_ciphertext.size());
	SensitiveBytesGuard decrypted_guard(decrypted);
	vBytes plain_chunk(STREAM_CHUNK_SIZE);
	SensitiveBytesGuard plain_chunk_guard(plain_chunk);

	std::size_t offset = 0;
	bool has_final_tag = false;

	while (offset < framed_ciphertext.size()) {
		if (framed_ciphertext.size() - offset < STREAM_FRAME_LEN_BYTES) {
			return false;
		}

		const std::uint32_t frame_len = readFrameLen(framed_ciphertext, offset);
		offset += STREAM_FRAME_LEN_BYTES;

		if (frame_len < crypto_secretstream_xchacha20poly1305_ABYTES ||
			frame_len > framed_ciphertext.size() - offset) {
			return false;
		}

		const std::size_t max_plain_chunk = static_cast<std::size_t>(frame_len) - crypto_secretstream_xchacha20poly1305_ABYTES;
		if (max_plain_chunk > plain_chunk.size()) {
			return false;
		}

		unsigned long long mlen = 0;
		unsigned char tag = 0;
		if (crypto_secretstream_xchacha20poly1305_pull(
				&state,
				plain_chunk.data(),
				&mlen,
				&tag,
				framed_ciphertext.data() + static_cast<std::ptrdiff_t>(offset),
				frame_len,
				nullptr,
				0) != 0) {
			return false;
		}
		if (mlen > max_plain_chunk) {
			return false;
		}
		decrypted.insert(
			decrypted.end(),
			plain_chunk.begin(),
			plain_chunk.begin() + static_cast<std::ptrdiff_t>(mlen)
		);

		offset += frame_len;
		if (tag == crypto_secretstream_xchacha20poly1305_TAG_FINAL) {
			has_final_tag = true;
			break;
		}
	}

	if (!has_final_tag || offset != framed_ciphertext.size()) {
		return false;
	}

	framed_ciphertext = std::move(decrypted);
	decrypted_guard.release();
	return true;
}

void deriveKeyFromPin(Key& out_key, std::size_t pin, const Salt& salt) {
	std::array<char, 32> pin_buf{};
	auto [ptr, ec] = std::to_chars(pin_buf.data(), pin_buf.data() + pin_buf.size(), pin);
	if (ec != std::errc{}) {
		sodium_memzero(pin_buf.data(), pin_buf.size());
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

	if (rc != 0) {
		throw std::runtime_error("KDF Error: Unable to derive encryption key.");
	}
}

[[nodiscard]] KdfMetadataVersion getKdfMetadataVersion(std::span<const Byte> data, std::size_t base_index) {
	if (!spanHasRange(data, base_index, KDF_METADATA_REGION_BYTES)) {
		return KdfMetadataVersion::none;
	}

	const auto header = std::span<const Byte>(
		data.data() + base_index + KDF_MAGIC_OFFSET,
		KDF_METADATA_MAGIC_V2.size()
	);

	const bool has_common_fields =
		data[base_index + KDF_ALG_OFFSET] == KDF_ALG_ARGON2ID13 &&
		data[base_index + KDF_SENTINEL_OFFSET] == KDF_SENTINEL;
	if (!has_common_fields) {
		return KdfMetadataVersion::none;
	}
	if (std::ranges::equal(header, KDF_METADATA_MAGIC_V2)) {
		return KdfMetadataVersion::v2_secretbox;
	}
	return KdfMetadataVersion::none;
}

[[nodiscard]] std::size_t generateRecoveryPin() {
	std::size_t pin = 0;
	while (pin == 0) {
		randombytes_buf(&pin, sizeof(pin));
	}
	return pin;
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

	if (payload.size() > prefix_len) {
		std::memmove(
			payload.data(),
			payload.data() + static_cast<std::ptrdiff_t>(prefix_len),
			payload.size() - prefix_len
		);
	}
	payload.resize(payload.size() - prefix_len);
	return decrypted_filename;
}
} // namespace

std::size_t getPin() {
	constexpr auto MAX_UINT64_STR = std::string_view{"18446744073709551615"};
	constexpr std::size_t MAX_PIN_LENGTH = 20;

	std::print("\nPIN: ");
	std::fflush(stdout);

	std::string input;
	char ch{};
	const bool is_tty = (isatty(STDIN_FILENO) != 0);
	TermiosGuard termios_guard;

	while (input.length() < MAX_PIN_LENGTH) {
		const ssize_t bytes_read = read(STDIN_FILENO, &ch, 1);
		if (bytes_read == 0) {
			break;
		}
		if (bytes_read < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		if (ch >= '0' && ch <= '9') {
			input.push_back(ch);
			if (is_tty) {
				std::print("*");
				std::fflush(stdout);
			}
		} else if ((ch == '\b' || ch == 127) && !input.empty()) {
			if (is_tty) {
				std::print("\b \b");
				std::fflush(stdout);
			}
			input.pop_back();
		} else if (ch == '\n' || ch == '\r') {
			break;
		}
	}

	std::println("");
	std::fflush(stdout);

	auto wipe_input = [&]() {
		if (!input.empty()) {
			sodium_memzero(input.data(), input.size());
		}
		input.clear();
	};

	if (input.empty() || (input.length() == MAX_PIN_LENGTH && input > MAX_UINT64_STR)) {
		wipe_input();
		return 0;
	}

	std::size_t result = 0;
	auto [ptr, ec] = std::from_chars(input.data(), input.data() + input.size(), result);
	if (ec != std::errc{} || ptr != input.data() + input.size()) {
		wipe_input();
		return 0;
	}

	wipe_input();
	return result;
}

std::size_t encryptDataFile(vBytes& profile_vec, vBytes& data_vec, const std::string& data_filename, bool has_mastodon_option) {
	const auto& offsets = has_mastodon_option ? MASTODON_OFFSETS : DEFAULT_OFFSETS;
	constexpr const char* CORRUPT_PROFILE_ERROR = "Internal Error: Corrupt profile template.";

	requireSpanRange(profile_vec, offsets.kdf_metadata, KDF_METADATA_REGION_BYTES, CORRUPT_PROFILE_ERROR);
	if (offsets.encrypted_file != profile_vec.size()) {
		throw std::runtime_error(CORRUPT_PROFILE_ERROR);
	}

	if (data_filename.empty() || data_filename.size() > static_cast<std::size_t>(std::numeric_limits<Byte>::max())) {
		throw std::runtime_error("Data File Error: Invalid data filename length.");
	}

	const std::size_t prefix_len = 1 + data_filename.size();
	if (data_vec.size() > std::numeric_limits<std::size_t>::max() - prefix_len) {
		throw std::runtime_error("Data File Error: Data file too large to encrypt.");
	}

	// Prefix plaintext with [filename_len][filename] in-place to avoid repeated begin() inserts on large payloads.
	const std::size_t payload_size = data_vec.size();
	data_vec.resize(payload_size + prefix_len);
	if (payload_size > 0) {
		std::memmove(
			data_vec.data() + static_cast<std::ptrdiff_t>(prefix_len),
			data_vec.data(),
			payload_size
		);
	}
	data_vec[0] = static_cast<Byte>(data_filename.size());
	std::memcpy(
		data_vec.data() + 1,
		data_filename.data(),
		data_filename.size()
	);

	SensitiveKeyData key;
	Salt salt{};
	StreamHeader stream_header{};
	const std::size_t pin = generateRecoveryPin();

	randombytes_buf(salt.data(), salt.size());
	deriveKeyFromPin(key.key, pin, salt);
	encryptWithSecretStream(data_vec, key.key, stream_header);

	profile_vec.reserve(profile_vec.size() + data_vec.size());
	profile_vec.insert(profile_vec.end(), data_vec.begin(), data_vec.end());
	data_vec.clear();

	// Write KDF metadata into the fixed 56-byte region.
	randombytes_buf(profile_vec.data() + offsets.kdf_metadata, KDF_METADATA_REGION_BYTES);
	profile_vec[offsets.kdf_metadata + KDF_MAGIC_OFFSET + 0] = 'K';
	profile_vec[offsets.kdf_metadata + KDF_MAGIC_OFFSET + 1] = 'D';
	profile_vec[offsets.kdf_metadata + KDF_MAGIC_OFFSET + 2] = 'F';
	profile_vec[offsets.kdf_metadata + KDF_MAGIC_OFFSET + 3] = '2';
	profile_vec[offsets.kdf_metadata + KDF_ALG_OFFSET] = KDF_ALG_ARGON2ID13;
	profile_vec[offsets.kdf_metadata + KDF_SENTINEL_OFFSET] = KDF_SENTINEL;

	requireSpanRange(profile_vec, offsets.kdf_metadata + KDF_SALT_OFFSET, salt.size(), CORRUPT_PROFILE_ERROR);
	requireSpanRange(profile_vec, offsets.kdf_metadata + KDF_NONCE_OFFSET, stream_header.size(), CORRUPT_PROFILE_ERROR);
	std::ranges::copy(salt, profile_vec.begin() + static_cast<std::ptrdiff_t>(offsets.kdf_metadata + KDF_SALT_OFFSET));
	std::ranges::copy(stream_header, profile_vec.begin() + static_cast<std::ptrdiff_t>(offsets.kdf_metadata + KDF_NONCE_OFFSET));

	return pin;
}

std::optional<std::string> decryptDataFile(vBytes& png_vec, bool is_mastodon_file) {
	const auto& offsets = is_mastodon_file ? MASTODON_OFFSETS : DEFAULT_OFFSETS;

	constexpr const char* CORRUPT_FILE_ERROR = "File Recovery Error: Embedded profile is corrupt.";
	requireSpanRange(png_vec, offsets.kdf_metadata, KDF_METADATA_REGION_BYTES, CORRUPT_FILE_ERROR);
	if (offsets.encrypted_file > png_vec.size()) {
		throw std::runtime_error(CORRUPT_FILE_ERROR);
	}

	const KdfMetadataVersion metadata_version = getKdfMetadataVersion(png_vec, offsets.kdf_metadata);
	if (metadata_version != KdfMetadataVersion::v2_secretbox) {
		throw std::runtime_error(
			"File Decryption Error: Unsupported legacy encrypted file format. "
			"Use an older pdvrdt release to recover this file.");
	}

	const std::size_t recovery_pin = getPin();

	SensitiveKeyData key;
	Salt salt{};
	StreamHeader stream_header{};

	requireSpanRange(png_vec, offsets.kdf_metadata + KDF_SALT_OFFSET, salt.size(), CORRUPT_FILE_ERROR);
	requireSpanRange(png_vec, offsets.kdf_metadata + KDF_NONCE_OFFSET, stream_header.size(), CORRUPT_FILE_ERROR);

	std::ranges::copy_n(
		png_vec.begin() + static_cast<std::ptrdiff_t>(offsets.kdf_metadata + KDF_SALT_OFFSET),
		salt.size(),
		salt.begin()
	);
	std::ranges::copy_n(
		png_vec.begin() + static_cast<std::ptrdiff_t>(offsets.kdf_metadata + KDF_NONCE_OFFSET),
		stream_header.size(),
		stream_header.begin()
	);

	deriveKeyFromPin(key.key, recovery_pin, salt);

	const std::size_t ciphertext_length = png_vec.size() - offsets.encrypted_file;
	const std::size_t min_stream_cipher_size =
		StreamHeader{}.size() + STREAM_FRAME_LEN_BYTES + crypto_secretstream_xchacha20poly1305_ABYTES;
	if (ciphertext_length < min_stream_cipher_size) {
		throw std::runtime_error(CORRUPT_FILE_ERROR);
	}

	if (offsets.encrypted_file != 0) {
		std::memmove(
			png_vec.data(),
			png_vec.data() + static_cast<std::ptrdiff_t>(offsets.encrypted_file),
			ciphertext_length
		);
	}
	png_vec.resize(ciphertext_length);

	requireSpanRange(png_vec, 0, StreamHeader{}.size(), CORRUPT_FILE_ERROR);
	StreamHeader embedded_header{};
	std::ranges::copy_n(png_vec.begin(), embedded_header.size(), embedded_header.begin());
	std::memmove(
		png_vec.data(),
		png_vec.data() + static_cast<std::ptrdiff_t>(embedded_header.size()),
		png_vec.size() - embedded_header.size()
	);
	png_vec.resize(png_vec.size() - embedded_header.size());

	if (embedded_header != stream_header) {
		throw std::runtime_error(CORRUPT_FILE_ERROR);
	}

	if (!decryptWithSecretStream(png_vec, key.key, stream_header)) {
		return std::nullopt;
	}

	return extractFilenamePrefix(png_vec);
}
