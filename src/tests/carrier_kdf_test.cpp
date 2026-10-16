// Include the carrier implementation so these tests can inspect the wire header
// and make deliberately invalid key/version pairs without exposing test hooks.
#include "../reddit_steg.cpp"

#include <bit>
#include <format>
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

std::uint64_t legacyKey(const SensitiveU64& pin) {
	SensitiveU64 key;
	deriveLegacyCarrierKeyFromPin(pin, key);
	return key.value;
}

using V3Key = std::array<Byte, CarrierKeys::V3_KEY_BYTES>;

// The public recovery path as recover runs it: decode first, then the PIN.
std::optional<vBytes> recoverPayload(vBytes png, const SensitiveU64& pin) {
	const auto carrier = decodeRedditRecoveryCarrier(png);
	requireTest(carrier.has_value(), "Test carrier was rejected before PIN use.");
	return extractRedditPngPayload(*carrier, pin);
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

// Tie-breaks only choose between equally costly changes, never what decodes,
// so test embeds under any key can share one fixed tie-break stream.
const TieBreakBits& testTies(std::size_t ordinals) {
	static const V3Key tie_key{};
	static std::optional<TieBreakBits> ties;
	ties.reset();
	ties.emplace(tie_key, ordinals);
	return *ties;
}

// Embed a header (and optionally a payload) under any version's key with any
// version byte -- the only way to build deliberately mismatched key/version
// pairs, since conceal itself writes only version 3.
template<typename Key>
void embedUnder(
	RedditPngCarrier& carrier, const Key& key,
	const std::array<Byte, HEADER_SIZE>& header, std::span<const Byte> payload = {}) {
	const KeyedPermutation permutation(carrier.rgb.size(), key);
	CarrierMasks masks(key);
	masks.preparePayload(payload.size());
	const TieBreakBits& ties = testTies(HEADER_SLOTS + payload.size() * 2U * GROUP_SAMPLES);
	const vBytes activity = buildActivityMap(carrier);
	embedHeader(carrier, activity, permutation, masks, ties, header);
	if (!payload.empty()) {
		embedPayload(carrier, activity, permutation, masks, ties, payload);
	}
}

template<typename Key>
std::array<Byte, HEADER_SIZE> headerUnder(const RedditPngCarrier& carrier, const Key& key) {
	const KeyedPermutation permutation(carrier.rgb.size(), key);
	const CarrierMasks masks(key);
	return extractHeader(carrier, permutation, masks);
}

std::array<Byte, HEADER_SIZE> headerWithVersion(std::span<const Byte> payload, Byte version) {
	auto header = makeHeader(payload);
	header[8] = version;
	writeLe32(header.data() + 20,
		pdvrdtCrc32Update(0, std::span<const Byte>(header).first(20)));
	return header;
}

template<typename Key>
vBytes embedPngUnder(const Key& key, std::span<const Byte> payload, Byte version) {
	auto carrier = makeTestCarrier();
	embedUnder(carrier, key, headerWithVersion(payload, version), payload);
	return encodeRgbPng(carrier);
}

void checkKnownAnswers() {
	// Frozen independently using Python ctypes.crypto_pwhash with literal
	// Argon2id13/2/67108864, 32 output bytes, salt b"pdvrdtcarrier-v2", and
	// pin.to_bytes(8, "big"); v2 is the first eight digest bytes big-endian, v3
	// is hashlib.blake2b(b"pdvrdt carrier v3", key=digest, digest_size=32).
	// Legacy answers use hashlib.blake2b with 32 output bytes and key
	// b"pdvrdt carrier v5".ljust(32, b"\0"). Keep these literals fixed:
	// changing a parameter or byte order must break the wire-format test.
	struct Vector {
		std::uint64_t pin;
		std::uint64_t v2;
		std::string_view v3;
		std::uint64_t legacy;
	};
	constexpr Vector vectors[] = {
		{1, 0xcacf13e77f898a90ULL,
			"09e577435b6f2e751bd987af05bb2325d2441c18bd345da2d1f2d9135307f435",
			0xceaeade28e02ec7dULL},
		{0x8000000000000000ULL, 0x193b2f36ba42f1edULL,
			"283be8a31692b302232f8a83a450705c6fa3caf00819fddf1976ed2f34ea36cb",
			0x54da2d2159d6b2beULL},
		{0xffffffffffffffffULL, 0x151428112b0af782ULL,
			"8554a8f6e280ca3b9b93701607aa949a113589f2882f750d30090b0330dce948",
			0x26ebb91e649c1b08ULL}
	};
	for (const auto& vector : vectors) {
		const SensitiveU64 pin(vector.pin);
		resetCalls();
		CarrierKeys keys;
		deriveCarrierKeysFromPin(pin, keys);
		std::string v3_hex;
		for (const Byte b : keys.v3) v3_hex += std::format("{:02x}", b);
		requireTest(keys.v2.value == vector.v2, "Carrier v2 KDF known-answer mismatch.");
		requireTest(v3_hex == vector.v3, "Carrier v3 KDF known-answer mismatch.");
		requireTest(pwhash_calls == 1 && legacy_hash_calls == 0,
			"Carrier v2/v3 keys did not come from exactly one Argon2 run.");
		requireTest(legacyKey(pin) == vector.legacy, "Legacy carrier KDF known-answer mismatch.");
	}
	std::cout << "[PASS] fixed v3, v2 and legacy carrier KDF vectors, including high-bit PINs\n";
}

// The original single-lookup Feistel, kept verbatim as the reference that the
// batched map() must reproduce exactly: versions 1 and 2 are recovery-only, so
// any change to their positions would orphan published images.
std::uint64_t referencePermutation(std::uint64_t size, std::uint64_t key, std::uint64_t ordinal) {
	std::array<std::uint64_t, 8> round_keys{};
	for (unsigned round = 0; round < 8; ++round) {
		round_keys[round] = splitmix64(
			key ^ PERMUTATION_DOMAIN ^ (static_cast<std::uint64_t>(round) * 0x9e3779b97f4a7c15ULL));
	}
	const auto bits = static_cast<unsigned>(std::bit_width(size - 1U));
	const unsigned left_init = bits / 2U;
	const unsigned right_init = bits - left_init;
	const auto mask_for = [](unsigned b) { return (std::uint64_t{1} << b) - 1U; };
	std::uint64_t value = ordinal;
	do {
		unsigned left_bits = left_init;
		unsigned right_bits = right_init;
		std::uint64_t left = value >> right_bits;
		std::uint64_t right = value & mask_for(right_bits);
		for (unsigned round = 0; round < 8; ++round) {
			const std::uint64_t function = splitmix64(right ^ round_keys[round]);
			const std::uint64_t next_right = (left ^ function) & mask_for(left_bits);
			left = right;
			right = next_right;
			std::swap(left_bits, right_bits);
		}
		value = (left << right_bits) | right;
	} while (value >= size);
	return value;
}

void checkBatchedPermutation() {
	for (const std::uint64_t size : {2ULL, 3ULL, 577ULL, 4096ULL, 4097ULL, 12288ULL, 1000003ULL}) {
		const std::uint64_t key = 0x0123456789abcdefULL ^ size;
		const KeyedPermutation permutation(size, key);
		const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(size, 5000));
		const std::uint64_t first = size - count;
		std::vector<std::size_t> batch(count);
		permutation.map(first, batch);
		std::vector<bool> seen(count == size ? size : 0);
		for (std::size_t i = 0; i < count; ++i) {
			requireTest(batch[i] == referencePermutation(size, key, first + i),
				"Batched permutation differs from the original single lookup.");
			requireTest(batch[i] < size, "Permutation left the sample domain.");
			if (!seen.empty()) {
				requireTest(!seen[batch[i]], "Permutation repeated a sample.");
				seen[batch[i]] = true;
			}
		}
		// Version 3 must also be a bijection of the exact domain.
		const V3Key v3_key{static_cast<Byte>(size), 7};
		const KeyedPermutation v3(size, v3_key);
		if (size <= 1000003ULL) {
			std::vector<std::size_t> all(static_cast<std::size_t>(size));
			v3.map(0, all);
			std::vector<bool> hit(static_cast<std::size_t>(size));
			for (const std::size_t sample : all) {
				requireTest(sample < size && !hit[sample], "Carrier v3 permutation is not a bijection.");
				hit[sample] = true;
			}
		}
	}
	std::cout << "[PASS] batched v1/v2 lookups match the original Feistel; v3 is a bijection\n";
}

void checkGoldenFixtures(const fs::path& tests) {
	struct Fixture {
		const char* name;
		std::uint64_t pin;
		unsigned domain_parity;
		Byte version;
	};
	constexpr Fixture fixtures[] = {
		{"reddit_even_domain", 17919152839358402291ULL, 0, 1},
		{"reddit_odd_domain", 1042468851987991096ULL, 1, 1},
		{"reddit_v2_even_domain", 5170514201323062387ULL, 0, 2},
		{"reddit_v2_odd_domain", 16097849665453844791ULL, 1, 2},
		{"reddit_v3_even_domain", 13160193868159090119ULL, 0, 3},
		{"reddit_v3_odd_domain", 3730076343303853748ULL, 1, 3}
	};
	for (const auto& fixture : fixtures) {
		vBytes png = readTestFile(tests / "golden" / fixture.name / "embedded.png");
		const auto decoded = decodeCarrier(png);
		requireTest((std::bit_width(decoded.carrier.rgb.size() - 1U) & 1U) == fixture.domain_parity,
			"Golden fixture has the wrong permutation-domain parity.");
		const SensitiveU64 pin(fixture.pin);
		CarrierKeys keys;
		deriveCarrierKeysFromPin(pin, keys);
		const auto old_payload = fixture.version == 3
			? tryExtractPayloadWithKey(decoded.carrier, keys.v3, 3)
			: tryExtractPayloadWithKey(decoded.carrier,
				fixture.version == 1 ? legacyKey(pin) : keys.v2.value, fixture.version);
		requireTest(old_payload.has_value() && isPdvrdtRedditProfile(*old_payload),
			"Golden fixture is not a valid carrier profile of its version.");
		if (fixture.version == 3) {
			requireTest(!tryExtractPayloadWithKey(decoded.carrier, keys.v2.value, 2),
				"Carrier v3 fixture exposes its magic under the 64-bit v2 key.");
		}
		resetCalls();
		const auto recovered = recoverPayload(png, pin);
		requireTest(recovered == old_payload, "Public recovery failed for a golden Reddit fixture.");
		requireTest(pwhash_calls == 1, "Carrier recovery did not use exactly one Argon2 run.");
		if (fixture.version == 1) {
			requireTest(legacy_hash_calls == 1 && !legacy_before_pwhash,
				"Legacy recovery did not attempt the Argon2 keys before the legacy key.");
		} else {
			requireTest(legacy_hash_calls == 0, "Carrier v2/v3 recovery fell through to the legacy key.");
		}

		fail_pwhash = true;
		resetCalls();
		requireThrows([&] { (void)recoverPayload(png, pin); },
			"An Argon2 failure was swallowed during carrier recovery.");
		fail_pwhash = false;
		requireTest(pwhash_calls == 1 && legacy_hash_calls == 0,
			"Argon2 failure fell back to the cheap legacy derivation.");
	}
	std::cout << "[PASS] carrier v1, v2 and v3 golden fixtures recover for both domain parities\n";
	std::cout << "[PASS] Argon2 failure propagates without cheap fallback\n";
}

void checkCurrentCarrier() {
	const SensitiveU64 pin(1);
	CarrierKeys keys;
	deriveCarrierKeysFromPin(pin, keys);
	const SensitiveU64 legacy_key(legacyKey(pin));
	const vBytes profile = makeTestProfile();
	auto carrier = makeTestCarrier();
	const vBytes png = embedRedditPngPayload(carrier, keys, profile);
	const auto header = headerUnder(carrier, keys.v3);
	requireTest(header[8] == 3 && header[9] == 2, "New conceal did not emit carrier v3 / scheme 2.");
	requireTest(tryExtractPayloadWithKey(carrier, keys.v3, 3) == std::optional<vBytes>(profile),
		"Current carrier did not decode under the Argon2-derived v3 key.");
	requireTest(!tryExtractPayloadWithKey(carrier, keys.v2.value, 2),
		"New carrier exposes its magic under the 64-bit v2 key.");
	requireTest(!tryExtractPayloadWithKey(carrier, legacy_key.value, 1),
		"New carrier exposes its magic under the cheap legacy PIN key.");
	resetCalls();
	requireTest(recoverPayload(png, pin) == std::optional<vBytes>(profile),
		"Public recovery failed for carrier v3.");
	requireTest(pwhash_calls == 1 && legacy_hash_calls == 0,
		"Current carrier recovery unexpectedly attempted the legacy key.");
	const SensitiveU64 wrong_pin(2);
	resetCalls();
	requireTest(!recoverPayload(png, wrong_pin), "Wrong PIN returned a carrier payload.");
	requireTest(pwhash_calls == 1, "Wrong PIN bypassed Argon2.");
	std::cout << "[PASS] emitted carrier v3 requires the new key and rejects a wrong PIN\n";

	// A header of another version under each key must be an error, not a reason
	// to try the next key. Its header CRC is valid so only version binding can
	// reject these deliberately mismatched pairs.
	struct Mismatch {
		const char* what;
		vBytes png;
		unsigned legacy_calls;
	};
	const Mismatch mismatches[] = {
		{"v2 header under the v3 key", embedPngUnder(keys.v3, profile, 2), 0},
		{"v1 header under the v3 key", embedPngUnder(keys.v3, profile, 1), 0},
		{"v3 header under the v2 key", embedPngUnder(keys.v2.value, profile, 3), 0},
		{"v1 header under the v2 key", embedPngUnder(keys.v2.value, profile, 1), 0},
		{"v3 header under the legacy key", embedPngUnder(legacy_key.value, profile, 3), 1},
		{"v2 header under the legacy key", embedPngUnder(legacy_key.value, profile, 2), 1}
	};
	for (const auto& mismatch : mismatches) {
		resetCalls();
		requireThrows([&] { (void)recoverPayload(mismatch.png, pin); },
			std::format("Accepted a {}.", mismatch.what));
		requireTest(pwhash_calls == 1 && legacy_hash_calls == mismatch.legacy_calls,
			std::format("A {} fell back to another key.", mismatch.what));
	}
	std::cout << "[PASS] v3, v2 and legacy carrier versions are bound to their keys\n";

	auto bad_header = carrier;
	auto corrupt_header = header;
	corrupt_header[20] ^= 1U;
	embedUnder(bad_header, keys.v3, corrupt_header);
	resetCalls();
	requireThrows([&] { (void)recoverPayload(encodeRgbPng(bad_header), pin); },
		"Current header CRC corruption was not reported.");
	requireTest(legacy_hash_calls == 0, "Header corruption triggered legacy fallback.");
	auto bad_payload = carrier;
	const KeyedPermutation permutation(carrier.rgb.size(), keys.v3);
	bad_payload.rgb[permutation(HEADER_SLOTS)] ^= 1U;
	resetCalls();
	requireThrows([&] { (void)recoverPayload(encodeRgbPng(bad_payload), pin); },
		"Current payload CRC corruption was not reported.");
	requireTest(legacy_hash_calls == 0, "Payload corruption triggered legacy fallback.");
	std::cout << "[PASS] recognized current carrier corruption never downgrades\n";

	auto generic_carrier = makeTestCarrier();
	const vBytes generic_payload{'g', 'e', 'n', 'e', 'r', 'i', 'c'};
	const vBytes generic_png = embedRedditPngPayload(generic_carrier, keys, generic_payload);
	resetCalls();
	requireTest(!recoverPayload(generic_png, pin), "Generic payload was exposed as a pdvrdt profile.");
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
	// The legacy key hashes the eight PIN bytes; the v3 key step hashes its
	// domain string under the Argon2 output and is part of the current path.
	if (input_length == sizeof(std::uint64_t)) {
		++legacy_hash_calls;
		legacy_before_pwhash = legacy_before_pwhash || pwhash_calls == 0;
	}
	return __real_crypto_generichash(out, outlen, input, input_length, key, key_length);
}

int main(int argc, char** argv) {
	try {
		if (argc != 2 || sodium_init() < 0) return 2;
		checkKnownAnswers();
		checkBatchedPermutation();
		checkGoldenFixtures(fs::path(argv[1]));
		checkCurrentCarrier();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "[FAIL] " << error.what() << '\n';
		return 1;
	}
}
