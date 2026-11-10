#include "key_codec.h"

#include <cstring>

namespace {

constexpr std::string_view ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
constexpr std::size_t BODY_CHARS = 22;
constexpr std::uint32_t CHECK_MODULUS = 58 * 58;
constexpr std::string_view CHECK_DOMAIN = "pdvrdt key check v1";

static_assert(KEY_TEXT_CHARS == BODY_CHARS + 2);

[[nodiscard]] constexpr bool isSeparator(char c) noexcept {
	return c == ' ' || c == '\t' || c == '-';
}

[[nodiscard]] int base58Digit(char c) noexcept {
	const auto pos = ALPHABET.find(c);
	return pos == std::string_view::npos ? -1 : static_cast<int>(pos);
}

[[nodiscard]] std::uint32_t checkValue(const std::array<Byte, RecoveryKey::KEY_BYTES>& key) noexcept {
	std::array<Byte, CHECK_DOMAIN.size() + RecoveryKey::KEY_BYTES> msg{};
	ScopedWipe msg_wipe{msg};
	std::memcpy(msg.data(), CHECK_DOMAIN.data(), CHECK_DOMAIN.size());
	std::memcpy(msg.data() + CHECK_DOMAIN.size(), key.data(), key.size());

	std::array<Byte, crypto_generichash_BYTES> hash{};
	ScopedWipe hash_wipe{hash};
	crypto_generichash(hash.data(), hash.size(), msg.data(), msg.size(), nullptr, 0);
	const std::uint32_t be =
		(static_cast<std::uint32_t>(hash[0]) << 24) |
		(static_cast<std::uint32_t>(hash[1]) << 16) |
		(static_cast<std::uint32_t>(hash[2]) << 8) |
		static_cast<std::uint32_t>(hash[3]);
	return be % CHECK_MODULUS;
}

} // namespace

void encodeKeyDisplay(const RecoveryKey& recovery_key, KeyDisplay& out) {
	std::array<char, KEY_TEXT_CHARS> chars{};
	ScopedWipe chars_wipe{chars};
	std::array<Byte, RecoveryKey::KEY_BYTES> work = recovery_key.key;
	ScopedWipe work_wipe{work};

	// Repeated long division by 58, least significant digit last.
	for (std::size_t i = BODY_CHARS; i-- > 0;) {
		std::uint32_t rem = 0;
		for (auto& b : work) {
			const std::uint32_t cur = (rem << 8) | b;
			b = static_cast<Byte>(cur / 58);
			rem = cur % 58;
		}
		chars[i] = ALPHABET[rem];
	}
	const std::uint32_t check = checkValue(recovery_key.key);
	chars[BODY_CHARS] = ALPHABET[check / 58];
	chars[BODY_CHARS + 1] = ALPHABET[check % 58];

	std::size_t o = 0;
	for (std::size_t i = 0; i < chars.size(); ++i) {
		if (i != 0 && i % 4 == 0) out[o++] = '-';
		out[o++] = chars[i];
	}
}

std::expected<void, KeyParseError> parseRecoveryKeyText(std::string_view text, RecoveryKey& out) {
	out.clear();

	std::array<char, KEY_TEXT_CHARS> sig{};
	ScopedWipe sig_wipe{sig};
	std::size_t n = 0;
	for (const char c : text) {
		if (isSeparator(c)) continue;
		if (n == sig.size()) return std::unexpected(KeyParseError::malformed);
		sig[n++] = c;
	}

	if (n != KEY_TEXT_CHARS) return std::unexpected(KeyParseError::malformed);

	std::array<int, KEY_TEXT_CHARS> digits{};
	ScopedWipe digits_wipe{digits};
	for (std::size_t i = 0; i < n; ++i) {
		digits[i] = base58Digit(sig[i]);
		if (digits[i] < 0) return std::unexpected(KeyParseError::malformed);
	}

	std::array<Byte, RecoveryKey::KEY_BYTES> key{};
	ScopedWipe key_wipe{key};
	for (std::size_t i = 0; i < BODY_CHARS; ++i) {
		auto carry = static_cast<std::uint32_t>(digits[i]);
		for (std::size_t j = key.size(); j-- > 0;) {
			const std::uint32_t cur = static_cast<std::uint32_t>(key[j]) * 58 + carry;
			key[j] = static_cast<Byte>(cur & 0xFF);
			carry = cur >> 8;
		}
		// 22 Base58 digits reach past 2^128; anything above it is no KEY.
		if (carry != 0) return std::unexpected(KeyParseError::malformed);
	}

	Byte any = 0;
	for (const Byte b : key) any |= b;
	if (any == 0) return std::unexpected(KeyParseError::malformed);

	const auto expected_check = static_cast<std::uint32_t>(digits[BODY_CHARS] * 58 + digits[BODY_CHARS + 1]);
	if (checkValue(key) != expected_check) return std::unexpected(KeyParseError::checksum);

	out.kind = RecoveryKey::Kind::key128;
	out.key = key;
	return {};
}
