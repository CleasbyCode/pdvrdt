// Include the carrier implementation so these tests can inspect the wire header
// and make deliberately invalid key/version pairs without exposing test hooks.
#include "../reddit_steg.cpp"

#include <bit>
#include <fstream>
#include <iostream>
#include <iterator>

namespace {

unsigned pwhash_calls = 0;
unsigned legacy_hash_calls = 0;
bool fail_pwhash = false;
bool legacy_before_pwhash = false;

void requireTest(bool condition, std::string_view message) {
	if (!condition) throw std::runtime_error(std::string(message));
}

template<typename Function>
void requireThrows(Function&& function, std::string_view message) {
	bool threw = false;
	try {
		function();
	} catch (const std::runtime_error&) {
		threw = true;
	}
	requireTest(threw, message);
}

void resetCalls() {
	pwhash_calls = 0;
	legacy_hash_calls = 0;
	legacy_before_pwhash = false;
}

vBytes readTestFile(const fs::path& path) {
	std::ifstream input(path, std::ios::binary);
	requireTest(static_cast<bool>(input), "Unable to open golden Reddit fixture.");
	return vBytes(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

RedditPngCarrier makeTestCarrier() {
	RedditPngCarrier carrier{.width = 64, .height = 64};
	carrier.rgb.resize(static_cast<std::size_t>(carrier.width) * carrier.height * 3U);
	for (std::size_t i = 0; i < carrier.rgb.size(); ++i) {
		carrier.rgb[i] = static_cast<Byte>((i * 73U + i / 11U) & 0xffU);
	}
	carrier.payload_capacity = theoreticalCapacity(carrier.rgb.size());
	return carrier;
}

// A syntactically complete profile is enough for the carrier API; authenticated
// decryption is covered by run_golden_tests.sh and run_roundtrip_tests.sh.
vBytes makeTestProfile() {
	vBytes profile(256, 0);
	std::copy(KDF_METADATA_MAGIC_V2.begin(), KDF_METADATA_MAGIC_V2.end(),
		profile.begin() + static_cast<std::ptrdiff_t>(DEFAULT_OFFSETS.kdf_metadata));
	profile[DEFAULT_OFFSETS.kdf_metadata + KDF_ALG_OFFSET] = KDF_ALG_ARGON2ID13;
	profile[DEFAULT_OFFSETS.kdf_metadata + KDF_SENTINEL_OFFSET] = KDF_SENTINEL;
	std::copy(PDVRDT_SIG.begin(), PDVRDT_SIG.end(),
		profile.begin() + static_cast<std::ptrdiff_t>(DEFAULT_OFFSETS.pdv_signature));
	return profile;
}

void replaceHeader(
	RedditPngCarrier& carrier,
	std::uint64_t key,
	const std::array<Byte, HEADER_SIZE>& header) {
	const KeyedPermutation permutation(carrier.rgb.size(), key);
	const vBytes activity = buildActivityMap(carrier);
	embedHeader(carrier, activity, permutation, key, header);
}

void setTestVersion(RedditPngCarrier& carrier, std::uint64_t key, Byte version) {
	const KeyedPermutation permutation(carrier.rgb.size(), key);
	auto header = extractHeader(carrier, permutation, key);
	header[8] = version;
	writeLe32(header.data() + 20,
		pdvrdtCrc32Update(0, std::span<const Byte>(header).first(20)));
	replaceHeader(carrier, key, header);
}

void checkKnownAnswers() {
	// Frozen independently using Python ctypes.crypto_pwhash with literal
	// Argon2id13/2/67108864, 32 output bytes, salt b"pdvrdtcarrier-v2", and
	// pin.to_bytes(8, "big"). Legacy answers use hashlib.blake2b with 32 output
	// bytes and key b"pdvrdt carrier v5".ljust(32, b"\0"). Keep these literals fixed:
	// changing a parameter or byte order must break the wire-format test.
	struct Vector {
		std::uint64_t pin;
		std::uint64_t current;
		std::uint64_t legacy;
	};
	constexpr Vector vectors[] = {
		{1, 0xcacf13e77f898a90ULL, 0xceaeade28e02ec7dULL},
		{0x8000000000000000ULL, 0x193b2f36ba42f1edULL, 0x54da2d2159d6b2beULL},
		{0xffffffffffffffffULL, 0x151428112b0af782ULL, 0x26ebb91e649c1b08ULL}
	};
	for (const auto& vector : vectors) {
		const SensitiveU64 pin(vector.pin);
		resetCalls();
		requireTest(deriveCarrierKeyFromPin(pin) == vector.current, "Carrier v2 KDF known-answer mismatch.");
		requireTest(pwhash_calls == 1 && legacy_hash_calls == 0, "Carrier v2 bypassed Argon2.");
		requireTest(deriveLegacyCarrierKeyFromPin(pin) == vector.legacy, "Legacy carrier KDF known-answer mismatch.");
	}
	std::cout << "[PASS] fixed current and legacy carrier KDF vectors, including high-bit PINs\n";
}

void checkLegacyFixtures(const fs::path& tests) {
	struct Fixture {
		const char* name;
		std::uint64_t pin;
		unsigned domain_parity;
	};
	constexpr Fixture fixtures[] = {
		{"reddit_even_domain", 17919152839358402291ULL, 0},
		{"reddit_odd_domain", 1042468851987991096ULL, 1}
	};
	for (const auto& fixture : fixtures) {
		const vBytes png = readTestFile(tests / "golden" / fixture.name / "embedded.png");
		const auto decoded = decodeCarrier(png);
		requireTest((std::bit_width(decoded.carrier.rgb.size() - 1U) & 1U) == fixture.domain_parity,
			"Golden fixture has the wrong permutation-domain parity.");
		const SensitiveU64 pin(fixture.pin);
		const SensitiveU64 legacy_key(deriveLegacyCarrierKeyFromPin(pin));
		const auto old_payload = tryExtractPayloadWithKey(decoded.carrier, legacy_key.value, 1);
		requireTest(old_payload.has_value() && isPdvrdtRedditProfile(*old_payload),
			"Golden fixture is not a valid carrier v1 profile.");
		resetCalls();
		const auto recovered = extractRedditPngPayload(png, pin);
		requireTest(recovered == old_payload, "Public recovery failed for a legacy Reddit fixture.");
		requireTest(pwhash_calls == 1 && legacy_hash_calls == 1 && !legacy_before_pwhash,
			"Legacy recovery did not attempt current derivation before the legacy key.");

		fail_pwhash = true;
		resetCalls();
		requireThrows([&] { (void)extractRedditPngPayload(png, pin); },
			"An Argon2 failure was swallowed during legacy recovery.");
		fail_pwhash = false;
		requireTest(pwhash_calls == 1 && legacy_hash_calls == 0,
			"Argon2 failure fell back to the cheap legacy derivation.");
	}
	std::cout << "[PASS] existing carrier v1 fixtures recover for both domain parities\n";
	std::cout << "[PASS] Argon2 failure propagates without cheap fallback\n";
}

void checkCurrentCarrier() {
	const SensitiveU64 pin(1);
	const SensitiveU64 key(deriveCarrierKeyFromPin(pin));
	const SensitiveU64 legacy_key(deriveLegacyCarrierKeyFromPin(pin));
	const vBytes profile = makeTestProfile();
	auto carrier = makeTestCarrier();
	const vBytes png = embedRedditPngPayload(carrier, key.value, profile);
	const KeyedPermutation permutation(carrier.rgb.size(), key.value);
	const auto header = extractHeader(carrier, permutation, key.value);
	requireTest(header[8] == 2 && header[9] == 2, "New conceal did not emit carrier v2 / scheme 2.");
	requireTest(tryExtractPayloadWithKey(carrier, key.value, 2) == std::optional<vBytes>(profile),
		"Current carrier did not decode under the Argon2-derived key.");
	requireTest(!tryExtractPayloadWithKey(carrier, legacy_key.value, 1),
		"New carrier exposes its magic under the cheap legacy PIN key.");
	resetCalls();
	requireTest(extractRedditPngPayload(png, pin) == std::optional<vBytes>(profile),
		"Public recovery failed for carrier v2.");
	requireTest(pwhash_calls == 1 && legacy_hash_calls == 0,
		"Current carrier recovery unexpectedly attempted the legacy key.");
	const SensitiveU64 wrong_pin(2);
	resetCalls();
	requireTest(!extractRedditPngPayload(png, wrong_pin), "Wrong PIN returned a carrier payload.");
	requireTest(pwhash_calls == 1, "Wrong PIN bypassed Argon2.");
	std::cout << "[PASS] emitted carrier v2 requires the new key and rejects a wrong PIN\n";

	// A v1 header encoded with the current key must be an error, not a reason
	// to try the legacy key. Its header CRC is valid so only version binding
	// can reject this deliberately mismatched pair.
	auto current_v1 = carrier;
	setTestVersion(current_v1, key.value, 1);
	resetCalls();
	requireThrows([&] { (void)extractRedditPngPayload(encodeRgbPng(current_v1), pin); },
		"Accepted a legacy version under the current key.");
	requireTest(pwhash_calls == 1 && legacy_hash_calls == 0,
		"Recognized current header fell back after a version mismatch.");

	auto legacy_v2 = makeTestCarrier();
	const vBytes legacy_v2_png = embedRedditPngPayload(legacy_v2, legacy_key.value, profile);
	resetCalls();
	requireThrows([&] { (void)extractRedditPngPayload(legacy_v2_png, pin); },
		"Accepted carrier v2 under the cheap legacy key.");
	requireTest(pwhash_calls == 1 && legacy_hash_calls == 1,
		"Wrong key/version pair did not reach strict legacy validation.");
	std::cout << "[PASS] current and legacy carrier versions are bound to their KDFs\n";

	auto bad_header = carrier;
	auto corrupt_header = header;
	corrupt_header[20] ^= 1U;
	replaceHeader(bad_header, key.value, corrupt_header);
	resetCalls();
	requireThrows([&] { (void)extractRedditPngPayload(encodeRgbPng(bad_header), pin); },
		"Current header CRC corruption was not reported.");
	requireTest(legacy_hash_calls == 0, "Header corruption triggered legacy fallback.");
	auto bad_payload = carrier;
	bad_payload.rgb[permutation(HEADER_SLOTS)] ^= 1U;
	resetCalls();
	requireThrows([&] { (void)extractRedditPngPayload(encodeRgbPng(bad_payload), pin); },
		"Current payload CRC corruption was not reported.");
	requireTest(legacy_hash_calls == 0, "Payload corruption triggered legacy fallback.");
	std::cout << "[PASS] recognized current carrier corruption never downgrades to legacy\n";

	auto generic_carrier = makeTestCarrier();
	const vBytes generic_payload{'g', 'e', 'n', 'e', 'r', 'i', 'c'};
	const vBytes generic_png = embedRedditPngPayload(generic_carrier, key.value, generic_payload);
	resetCalls();
	requireTest(!extractRedditPngPayload(generic_png, pin), "Generic payload was exposed as a pdvrdt profile.");
	requireTest(pwhash_calls == 1 && legacy_hash_calls == 0,
		"A matched generic carrier triggered legacy fallback.");
	std::cout << "[PASS] a valid generic current carrier returns no profile without fallback\n";
}

} // namespace

extern "C" int __real_crypto_pwhash(
	unsigned char*, unsigned long long, const char*, unsigned long long,
	const unsigned char*, unsigned long long, std::size_t, int);

extern "C" int __wrap_crypto_pwhash(
	unsigned char* out, unsigned long long outlen,
	const char* password, unsigned long long password_length,
	const unsigned char* salt, unsigned long long opslimit, std::size_t memlimit, int algorithm) {
	++pwhash_calls;
	if (fail_pwhash) return -1;
	return __real_crypto_pwhash(out, outlen, password, password_length, salt, opslimit, memlimit, algorithm);
}

extern "C" int __real_crypto_generichash(
	unsigned char*, std::size_t, const unsigned char*, unsigned long long,
	const unsigned char*, std::size_t);

extern "C" int __wrap_crypto_generichash(
	unsigned char* out, std::size_t outlen, const unsigned char* input,
	unsigned long long input_length, const unsigned char* key, std::size_t key_length) {
	++legacy_hash_calls;
	legacy_before_pwhash = legacy_before_pwhash || pwhash_calls == 0;
	return __real_crypto_generichash(out, outlen, input, input_length, key, key_length);
}

int main(int argc, char** argv) {
	try {
		if (argc != 2 || sodium_init() < 0) return 2;
		checkKnownAnswers();
		checkLegacyFixtures(fs::path(argv[1]));
		checkCurrentCarrier();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "[FAIL] " << error.what() << '\n';
		return 1;
	}
}
