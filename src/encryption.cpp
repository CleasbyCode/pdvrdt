#include "encryption.h"
#include "png_utils.h"

namespace {

	constexpr std::size_t VALUE_BYTE_LENGTH_EIGHT = 8;

struct SensitiveKeyData {
	Key   key{};
	Nonce nonce{};

	// Generate fresh key and nonce (for encryption).
	SensitiveKeyData() {
		crypto_secretbox_keygen(key.data());
		randombytes_buf(nonce.data(), nonce.size());
	}

	// Load existing key and nonce from a data source (for decryption).
	SensitiveKeyData(std::span<const Byte> key_src, std::span<const Byte> nonce_src) {
		std::ranges::copy_n(key_src.begin(),   key.size(),   key.data());
		std::ranges::copy_n(nonce_src.begin(), nonce.size(), nonce.data());
	}

	~SensitiveKeyData() {
		sodium_memzero(key.data(),   key.size());
		sodium_memzero(nonce.data(), nonce.size());
	}

	SensitiveKeyData(const SensitiveKeyData&) = delete;
	SensitiveKeyData& operator=(const SensitiveKeyData&) = delete;
};

struct SyncGuard {
	bool old_status;
	SyncGuard() : old_status(std::cout.sync_with_stdio(false)) {}
	~SyncGuard() { std::cout.sync_with_stdio(old_status); }
};

#ifndef _WIN32
struct TermiosGuard {
	termios old;
	TermiosGuard() {
		tcgetattr(STDIN_FILENO, &old);
		termios newt = old;
		newt.c_lflag &= ~(ICANON | ECHO);
		tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	}
	~TermiosGuard() { tcsetattr(STDIN_FILENO, TCSANOW, &old); }
};
#endif

} // namespace

std::size_t getPin() {
	constexpr auto MAX_UINT64_STR = std::string_view{"18446744073709551615"};
	constexpr std::size_t MAX_PIN_LENGTH = 20;

	std::print("\nPIN: ");
	std::fflush(stdout);

	std::string input;
	char ch;

	SyncGuard sync_guard;

#ifdef _WIN32
	while (input.length() < MAX_PIN_LENGTH) {
		ch = _getch();
		if (ch >= '0' && ch <= '9') {
			input.push_back(ch);
			std::print("*");
			std::fflush(stdout);
		} else if (ch == '\b' && !input.empty()) {
			std::print("\b \b");
			std::fflush(stdout);
			input.pop_back();
		} else if (ch == '\r') {
			break;
		}
	}
#else
	TermiosGuard termios_guard;
	while (input.length() < MAX_PIN_LENGTH) {
		ssize_t bytes_read = read(STDIN_FILENO, &ch, 1);
		if (bytes_read <= 0) continue;
		if (ch >= '0' && ch <= '9') {
			input.push_back(ch);
			std::print("*");
			std::fflush(stdout);
		} else if ((ch == '\b' || ch == 127) && !input.empty()) {
			std::print("\b \b");
			std::fflush(stdout);
			input.pop_back();
		} else if (ch == '\n') {
			break;
		}
	}
#endif

	std::println("");
	std::fflush(stdout);

	if (input.empty() || (input.length() == MAX_PIN_LENGTH && input > MAX_UINT64_STR)) {
		return 0;
	}

	std::size_t result = 0;
	auto [ptr, ec] = std::from_chars(input.data(), input.data() + input.size(), result);
	if (ec != std::errc{}) return 0;

	return result;
}

std::size_t encryptDataFile(vBytes& profile_vec, vBytes& data_vec, const std::string& data_filename, bool has_mastodon_option) {

	const auto& offsets = has_mastodon_option ? MASTODON_OFFSETS : DEFAULT_OFFSETS;
	const Byte filename_length = profile_vec[offsets.filename - 1];


	// XOR-encrypt the data filename into the profile.
	randombytes_buf(profile_vec.data() + offsets.filename_xor_key, filename_length);
	std::ranges::transform(
		data_filename | std::views::take(filename_length),
		profile_vec | std::views::drop(offsets.filename_xor_key) | std::views::take(filename_length),
		profile_vec.begin() + offsets.filename,
		[](char a, Byte b) { return static_cast<Byte>(a) ^ b; }
	);

	// Generate key and nonce (RAII ensures cleanup on any exit path).
	SensitiveKeyData crypto;

	std::ranges::copy(crypto.key,   profile_vec.begin() + offsets.sodium_key);
	std::ranges::copy(crypto.nonce, profile_vec.begin() + offsets.nonce);

	// Retrieve the key fingerprint before obfuscation.
	// Encrypt data in-place (libsodium supports overlapping src/dst for _easy functions).
	const std::size_t
		key_fingerprint = getValue(profile_vec, offsets.sodium_key, VALUE_BYTE_LENGTH_EIGHT),
		plaintext_length = data_vec.size();

	data_vec.resize(plaintext_length + TAG_BYTES);

	if (crypto_secretbox_easy(data_vec.data(), data_vec.data(), plaintext_length, crypto.nonce.data(), crypto.key.data()) != 0) {
		throw std::runtime_error("crypto_secretbox_easy failed.");
	}

	// Append encrypted data to profile, then release data_vec memory.
	profile_vec.insert(profile_vec.end(), data_vec.begin(), data_vec.end());
	data_vec.clear();
	data_vec.shrink_to_fit();

	// XOR-obfuscate the stored key+nonce (48 bytes) using the first 8 bytes of the key as a rolling XOR mask.
	constexpr std::size_t
		XOR_MASK_LENGTH    = 8,
		SODIUM_KEYS_LENGTH = 48;

	const std::size_t
		mask_start = offsets.sodium_key,
		keys_start = offsets.sodium_key + XOR_MASK_LENGTH;

	for (std::size_t i = 0; i < SODIUM_KEYS_LENGTH; ++i) {
		profile_vec[keys_start + i] ^= profile_vec[mask_start + (i % XOR_MASK_LENGTH)];
	}

	// Overwrite the XOR mask with random data.
	std::size_t random_val;
	randombytes_buf(&random_val, sizeof random_val);
	updateValue(profile_vec, offsets.sodium_key, random_val, VALUE_BYTE_LENGTH_EIGHT);

	return key_fingerprint;
}

std::optional<std::string> decryptDataFile(vBytes& png_vec, bool is_mastodon_file) {
	const auto& offsets = is_mastodon_file ? MASTODON_OFFSETS : DEFAULT_OFFSETS;

	constexpr std::size_t
		XOR_MASK_LENGTH    = 8,
		SODIUM_KEYS_LENGTH = 48;

	// Restore the XOR mask from the user-provided recovery pin.
	const std::size_t recovery_pin = getPin();
	updateValue(png_vec, offsets.sodium_key, recovery_pin, VALUE_BYTE_LENGTH_EIGHT);

	// De-obfuscate the stored key+nonce using the rolling XOR mask.
	const std::size_t
		mask_start = offsets.sodium_key,
		keys_start = offsets.sodium_key + XOR_MASK_LENGTH;

	for (std::size_t i = 0; i < SODIUM_KEYS_LENGTH; ++i) {
		png_vec[keys_start + i] ^= png_vec[mask_start + (i % XOR_MASK_LENGTH)];
	}

	// Load key and nonce (RAII ensures cleanup on any exit path).
	SensitiveKeyData crypto(std::span<const Byte>(png_vec).subspan(offsets.sodium_key, 32), std::span<const Byte>(png_vec).subspan(offsets.nonce, 24));

	// Decrypt the filename.
	const Byte filename_length = png_vec[offsets.filename - 1];
	std::string decrypted_filename(filename_length, '\0');

	std::ranges::transform(
		png_vec | std::views::drop(offsets.filename) | std::views::take(filename_length),
		png_vec | std::views::drop(offsets.filename_xor_key) | std::views::take(filename_length),
		decrypted_filename.begin(),
		[](Byte a, Byte b) { return static_cast<char>(a ^ b); }
	);

	// Decrypt data in-place, writing to the start of png_vec to reuse the buffer.
	const std::size_t ciphertext_length = png_vec.size() - offsets.encrypted_file;
	const Byte* cipher_src = png_vec.data() + offsets.encrypted_file;

	if (crypto_secretbox_open_easy(png_vec.data(), cipher_src, ciphertext_length, crypto.nonce.data(), crypto.key.data()) != 0) {
		std::println(std::cerr, "\nDecryption failed!");
		return std::nullopt;
	}

	png_vec.resize(ciphertext_length - TAG_BYTES);
	return decrypted_filename;
}
