// Recovery KEY codec: display form, parsing, check characters, and refusal of
// the numeric PINs used before pdvrdt 6.0.
#include "../key_codec.h"

#include <algorithm>
#include <format>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void requireTest(bool condition, std::string_view message) {
	if (!condition) throw std::runtime_error(std::string(message));
}

std::array<Byte, RecoveryKey::KEY_BYTES> fromHex(std::string_view hex) {
	std::array<Byte, RecoveryKey::KEY_BYTES> out{};
	requireTest(hex.size() == out.size() * 2, "Bad test vector.");
	for (std::size_t i = 0; i < out.size(); ++i) {
		out[i] = static_cast<Byte>(std::stoi(std::string(hex.substr(i * 2, 2)), nullptr, 16));
	}
	return out;
}

std::string display(const RecoveryKey& recovery_key) {
	KeyDisplay text{};
	encodeKeyDisplay(recovery_key, text);
	return std::string(text.data(), text.size());
}

void checkVectors() {
	// Frozen independently in Python: Base58 (Bitcoin alphabet) of the 16 bytes
	// read big-endian, left-padded with '1' to 22 characters, then the check
	// BE32(hashlib.blake2b(b"pdvrdt key check v1" + key, digest_size=32)[:4])
	// % 3364 as two more characters. The last two vectors are the check values
	// 0 and 3363, so both ends of the check range are covered.
	struct Vector { std::string_view hex, text; };
	constexpr Vector vectors[] = {
		{"000102030405060708090a0b0c0d0e0f", "112d-rXXU-ifSr-RnXL-GbXg-8Eyf"},
		{"ffffffffffffffffffffffffffffffff", "YcVf-xkQb-6JRz-qk5k-F2tN-Lvdj"},
		{"00000000000000000000000000000001", "1111-1111-1111-1111-1111-12BZ"},
		{"80000000000000000000000000000000", "GokL-Usho-3eiV-vNYN-d1wg-fywf"},
		{"000000000000000000000000000001bb", "1111-1111-1111-1111-1111-8e11"},
		{"00000000000000000000000000003b26", "1111-1111-1111-1111-1115-W5zz"},
	};
	for (const auto& vector : vectors) {
		RecoveryKey recovery_key;
		recovery_key.kind = RecoveryKey::Kind::key128;
		recovery_key.key = fromHex(vector.hex);
		requireTest(display(recovery_key) == vector.text, std::format("Display mismatch for {}.", vector.hex));

		RecoveryKey parsed;
		requireTest(parseRecoveryKeyText(vector.text, parsed).has_value(),
			std::format("{} did not parse.", vector.text));
		requireTest(parsed.isKey() && parsed.key == recovery_key.key, std::format("{} parsed wrongly.", vector.text));

		// Separators are ignored anywhere; case is not.
		std::string loose;
		for (const char c : vector.text) {
			if (c != '-') loose += c;
			loose += ' ';
		}
		RecoveryKey loose_parsed;
		requireTest(parseRecoveryKeyText(loose, loose_parsed).has_value() && loose_parsed.key == recovery_key.key,
			"Spaced KEY did not parse.");
	}
	std::cout << "[PASS] KEY display and parse vectors, including both check-value extremes\n";
}

void checkRejections() {
	const auto rejects = [](std::string_view text, KeyParseError expected, std::string_view what) {
		RecoveryKey recovery_key;
		recovery_key.kind = RecoveryKey::Kind::key128;
		recovery_key.key.fill(0x5A);
		const auto result = parseRecoveryKeyText(text, recovery_key);
		requireTest(!result && result.error() == expected, std::format("Accepted {}.", what));
		const bool wiped = std::ranges::all_of(recovery_key.key, [](Byte b) { return b == 0; });
		requireTest(recovery_key.kind == RecoveryKey::Kind::none && wiped,
			std::format("Rejected {} left a secret behind.", what));
	};
	rejects("112d-rXXU-ifSr-RnXL-GbXg-8Eyg", KeyParseError::checksum, "a wrong check character");
	rejects("112d-rXXU-ifSr-RnXL-GbXh-8Eyf", KeyParseError::checksum, "a wrong body character");
	rejects("112D-rXXU-ifSr-RnXL-GbXg-8Eyf", KeyParseError::checksum, "a changed letter case");
	rejects("112d-rXXU-ifSr-RnXL-GbXg-8Ey", KeyParseError::malformed, "a short KEY");
	rejects("112d-rXXU-ifSr-RnXL-GbXg-8Eyff", KeyParseError::malformed, "a long KEY");
	rejects("112d-rXXU-ifSr-RnXL-GbXg-8Ey0", KeyParseError::malformed, "a non-Base58 '0'");
	rejects("112d-rXXU-ifSr-RnXL-GbXg-8EyI", KeyParseError::malformed, "a non-Base58 'I'");
	rejects("1111-1111-1111-1111-1111-1111", KeyParseError::malformed, "the all-zero KEY");
	// 22 Base58 digits can exceed 2^128.
	rejects("zzzz-zzzz-zzzz-zzzz-zzzz-zzzz", KeyParseError::malformed, "a KEY above 2^128");
	rejects("", KeyParseError::malformed, "an empty entry");
	rejects("12a4", KeyParseError::malformed, "a short mixed entry");
	std::cout << "[PASS] malformed entries and typos are rejected and leave nothing behind\n";
}

// Images made before pdvrdt 6.0 used a numeric PIN of up to 20 digits; this
// build reads no such image, so a PIN is not a well-formed entry.
void checkNumericPinsRefused() {
	for (const std::string_view text : {
			"1", "18446744073709551615", "10479510958359240708", "1047 9510-9583\t5924 0708"}) {
		RecoveryKey recovery_key;
		const auto result = parseRecoveryKeyText(text, recovery_key);
		requireTest(!result && result.error() == KeyParseError::malformed,
			std::format("Accepted the old numeric PIN {}.", text));
		requireTest(recovery_key.kind == RecoveryKey::Kind::none, "A refused PIN left a secret behind.");
	}
	std::cout << "[PASS] old numeric PINs are refused\n";
}

} // namespace

int main() {
	try {
		if (sodium_init() < 0) return 2;
		checkVectors();
		checkRejections();
		checkNumericPinsRefused();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "[FAIL] " << error.what() << '\n';
		return 1;
	}
}
