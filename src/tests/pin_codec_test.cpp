// Recovery KEY codec: display form, parsing, check characters and old PINs.
#include "../pin_codec.h"

#include <format>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void requireTest(bool condition, std::string_view message) {
	if (!condition) throw std::runtime_error(std::string(message));
}

std::array<Byte, RecoveryPin::KEY_BYTES> fromHex(std::string_view hex) {
	std::array<Byte, RecoveryPin::KEY_BYTES> out{};
	requireTest(hex.size() == out.size() * 2, "Bad test vector.");
	for (std::size_t i = 0; i < out.size(); ++i) {
		out[i] = static_cast<Byte>(std::stoi(std::string(hex.substr(i * 2, 2)), nullptr, 16));
	}
	return out;
}

std::string display(const RecoveryPin& pin) {
	KeyDisplay text{};
	encodeKeyDisplay(pin, text);
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
		RecoveryPin pin;
		pin.kind = RecoveryPin::Kind::key128;
		pin.key = fromHex(vector.hex);
		requireTest(display(pin) == vector.text, std::format("Display mismatch for {}.", vector.hex));

		RecoveryPin parsed;
		requireTest(parseRecoveryPinText(vector.text, parsed).has_value(),
			std::format("{} did not parse.", vector.text));
		requireTest(parsed.isKey() && parsed.key == pin.key, std::format("{} parsed wrongly.", vector.text));

		// Separators are ignored anywhere; case is not.
		std::string loose;
		for (const char c : vector.text) {
			if (c != '-') loose += c;
			loose += ' ';
		}
		RecoveryPin loose_parsed;
		requireTest(parseRecoveryPinText(loose, loose_parsed).has_value() && loose_parsed.key == pin.key,
			"Spaced KEY did not parse.");
	}
	std::cout << "[PASS] KEY display and parse vectors, including both check-value extremes\n";
}

void checkRejections() {
	const auto rejects = [](std::string_view text, PinParseError expected, std::string_view what) {
		RecoveryPin pin;
		pin.kind = RecoveryPin::Kind::legacy_pin;
		pin.legacy_value = 99;
		const auto result = parseRecoveryPinText(text, pin);
		requireTest(!result && result.error() == expected, std::format("Accepted {}.", what));
		requireTest(pin.kind == RecoveryPin::Kind::none && pin.legacy_value == 0,
			std::format("Rejected {} left a secret behind.", what));
	};
	rejects("112d-rXXU-ifSr-RnXL-GbXg-8Eyg", PinParseError::checksum, "a wrong check character");
	rejects("112d-rXXU-ifSr-RnXL-GbXh-8Eyf", PinParseError::checksum, "a wrong body character");
	rejects("112D-rXXU-ifSr-RnXL-GbXg-8Eyf", PinParseError::checksum, "a changed letter case");
	rejects("112d-rXXU-ifSr-RnXL-GbXg-8Ey", PinParseError::malformed, "a short KEY");
	rejects("112d-rXXU-ifSr-RnXL-GbXg-8Eyff", PinParseError::malformed, "a long KEY");
	rejects("112d-rXXU-ifSr-RnXL-GbXg-8Ey0", PinParseError::malformed, "a non-Base58 '0'");
	rejects("112d-rXXU-ifSr-RnXL-GbXg-8EyI", PinParseError::malformed, "a non-Base58 'I'");
	rejects("1111-1111-1111-1111-1111-1111", PinParseError::malformed, "the all-zero KEY");
	// 22 Base58 digits can exceed 2^128.
	rejects("zzzz-zzzz-zzzz-zzzz-zzzz-zzzz", PinParseError::malformed, "a KEY above 2^128");
	rejects("", PinParseError::malformed, "an empty entry");
	rejects("0", PinParseError::malformed, "PIN 0");
	rejects("0123", PinParseError::malformed, "a PIN with a leading zero");
	rejects("18446744073709551616", PinParseError::malformed, "a PIN above 2^64-1");
	rejects("123456789012345678901", PinParseError::malformed, "a 21-digit PIN");
	rejects("12a4", PinParseError::malformed, "a PIN with a letter");
	std::cout << "[PASS] malformed entries and typos are rejected and leave nothing behind\n";
}

void checkLegacyPins() {
	struct Vector { std::string_view text; std::uint64_t value; };
	constexpr Vector vectors[] = {
		{"1", 1},
		{"18446744073709551615", 18446744073709551615ULL},
		{"10479510958359240708", 10479510958359240708ULL},
		{"1047 9510-9583\t5924 0708", 10479510958359240708ULL},
	};
	for (const auto& vector : vectors) {
		RecoveryPin pin;
		requireTest(parseRecoveryPinText(vector.text, pin).has_value(), "Old PIN did not parse.");
		requireTest(pin.kind == RecoveryPin::Kind::legacy_pin && pin.legacy_value == vector.value,
			"Old PIN parsed wrongly.");
	}
	std::cout << "[PASS] old numeric PINs still parse\n";
}

} // namespace

int main() {
	try {
		if (sodium_init() < 0) return 2;
		checkVectors();
		checkRejections();
		checkLegacyPins();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "[FAIL] " << error.what() << '\n';
		return 1;
	}
}
