// Include the carrier implementation so these tests can inspect the wire header
// and make deliberately invalid key/version pairs without exposing test hooks.
#include "../reddit_steg.cpp"
#include "../pin_codec.h"

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

void setLegacyPin(RecoveryPin& pin, std::uint64_t value) {
	pin.clear();
	pin.kind = RecoveryPin::Kind::legacy_pin;
	pin.legacy_value = value;
}

void setKey(RecoveryPin& pin, std::string_view text) {
	requireTest(parseRecoveryPinText(text, pin).has_value() && pin.isKey(), "Test KEY did not parse.");
}

std::uint64_t legacyKey(const RecoveryPin& pin) {
	SensitiveU64 key;
	deriveLegacyCarrierKeyFromPin(pin, key);
	return key.value;
}

using ChachaKey = std::array<Byte, CarrierKeys::CHACHA_KEY_BYTES>;

std::string hexOf(std::span<const Byte> bytes) {
	std::string hex;
	for (const Byte b : bytes) hex += std::format("{:02x}", b);
	return hex;
}

// The public recovery path as recover runs it: decode first, then the KEY/PIN.
std::optional<vBytes> recoverPayload(vBytes png, const RecoveryPin& pin) {
	const auto carrier = decodeRedditRecoveryCarrier(png);
	requireTest(carrier.has_value(), "Test carrier was rejected before KEY/PIN use.");
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
	std::copy(KDF_METADATA_MAGIC_V3.begin(), KDF_METADATA_MAGIC_V3.end(),
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
	static const ChachaKey tie_key{};
	static std::optional<TieBreakBits> ties;
	ties.reset();
	ties.emplace(tie_key, ordinals);
	return *ties;
}

// Embed a header (and optionally a payload) under any version's key with any
// version byte -- the only way to build deliberately mismatched key/version
// pairs, since conceal itself writes only version 4.
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
		RecoveryPin pin;
		setLegacyPin(pin, vector.pin);
		resetCalls();
		CarrierKeys keys;
		deriveCarrierKeysFromPin(pin, keys);
		requireTest(keys.chacha_version == 3 && keys.has_v2, "An old PIN did not derive the v3/v2 keys.");
		requireTest(keys.v2.value == vector.v2, "Carrier v2 KDF known-answer mismatch.");
		requireTest(hexOf(keys.chacha) == vector.v3, "Carrier v3 KDF known-answer mismatch.");
		requireTest(pwhash_calls == 1 && legacy_hash_calls == 0,
			"Carrier v2/v3 keys did not come from exactly one Argon2 run.");
		requireTest(legacyKey(pin) == vector.legacy, "Legacy carrier KDF known-answer mismatch.");
	}
	std::cout << "[PASS] fixed v3, v2 and legacy carrier KDF vectors, including high-bit PINs\n";

	// Version 4, from a 128-bit KEY. Frozen the same way: Argon2id13/2/67108864,
	// 32 output bytes, salt b"pdvrdtcarrier-v4", the 16 raw KEY bytes as the
	// password; then hashlib.blake2b(b"pdvrdt carrier v4", key=digest,
	// digest_size=32). The KEYs are the display forms of 000102..0f, ff..ff and
	// 00..01 (see pin_codec_test.cpp).
	struct KeyVector {
		std::string_view key;
		std::string_view v4;
	};
	constexpr KeyVector key_vectors[] = {
		{"112d-rXXU-ifSr-RnXL-GbXg-8Eyf",
			"6eba642bddf8a83f77945ef0d588a818a6810090beb97729990957f6f97e91c8"},
		{"YcVf-xkQb-6JRz-qk5k-F2tN-Lvdj",
			"fd755da59b640cc22250b5c084822637830db45618bf590a4825e79dca02cb0e"},
		{"1111-1111-1111-1111-1111-12BZ",
			"053414080cbdeba96a730a7f0649d8408635e01a6b69649659997fda747983bf"}
	};
	for (const auto& vector : key_vectors) {
		RecoveryPin pin;
		setKey(pin, vector.key);
		resetCalls();
		CarrierKeys keys;
		deriveCarrierKeysFromPin(pin, keys);
		requireTest(keys.chacha_version == 4 && !keys.has_v2, "A KEY did not derive only the v4 key.");
		requireTest(hexOf(keys.chacha) == vector.v4, "Carrier v4 KDF known-answer mismatch.");
		requireTest(pwhash_calls == 1 && legacy_hash_calls == 0,
			"The carrier v4 key did not come from exactly one Argon2 run.");
		requireThrows([&] { (void)legacyKey(pin); },
			"A KEY was accepted by the version-1 PIN hash.");
	}
	std::cout << "[PASS] fixed v4 carrier KDF vectors; a KEY never reaches the old derivations\n";
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
		// Versions 3 and 4 must also be a bijection of the exact domain.
		const ChachaKey v3_key{static_cast<Byte>(size), 7};
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
	std::cout << "[PASS] batched v1/v2 lookups match the original Feistel; v3/v4 is a bijection\n";
}

void checkGoldenFixtures(const fs::path& tests) {
	struct Fixture {
		const char* name;
		std::string_view secret;   // a KEY for version 4, an old PIN before it
		unsigned domain_parity;
		Byte version;
	};
	constexpr Fixture fixtures[] = {
		{"reddit_even_domain", "17919152839358402291", 0, 1},
		{"reddit_odd_domain", "1042468851987991096", 1, 1},
		{"reddit_v2_even_domain", "5170514201323062387", 0, 2},
		{"reddit_v2_odd_domain", "16097849665453844791", 1, 2},
		{"reddit_v3_even_domain", "13160193868159090119", 0, 3},
		{"reddit_v3_odd_domain", "3730076343303853748", 1, 3},
		{"reddit_v4_even_domain", "ERWk-iXmx-QZgW-tvfs-pSf8-Uhxi", 0, 4},
		{"reddit_v4_odd_domain", "TEDt-XPwo-TPMi-J1Fa-MLeJ-MDei", 1, 4}
	};
	for (const auto& fixture : fixtures) {
		vBytes png = readTestFile(tests / "golden" / fixture.name / "embedded.png");
		const auto decoded = decodeCarrier(png);
		requireTest((std::bit_width(decoded.carrier.rgb.size() - 1U) & 1U) == fixture.domain_parity,
			"Golden fixture has the wrong permutation-domain parity.");
		RecoveryPin pin;
		requireTest(parseRecoveryPinText(fixture.secret, pin).has_value(), "Golden fixture secret did not parse.");
		requireTest(pin.isKey() == (fixture.version == 4), "Golden fixture secret is of the wrong kind.");
		CarrierKeys keys;
		deriveCarrierKeysFromPin(pin, keys);
		const auto old_payload = fixture.version >= 3
			? tryExtractPayloadWithKey(decoded.carrier, keys.chacha, fixture.version)
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
			requireTest(legacy_hash_calls == 0, "Carrier v2-v4 recovery fell through to the legacy key.");
		}

		fail_pwhash = true;
		resetCalls();
		requireThrows([&] { (void)recoverPayload(png, pin); },
			"An Argon2 failure was swallowed during carrier recovery.");
		fail_pwhash = false;
		requireTest(pwhash_calls == 1 && legacy_hash_calls == 0,
			"Argon2 failure fell back to the cheap legacy derivation.");
	}
	std::cout << "[PASS] carrier v1, v2, v3 and v4 golden fixtures recover for both domain parities\n";
	std::cout << "[PASS] Argon2 failure propagates without cheap fallback\n";
}

void checkCurrentCarrier() {
	RecoveryPin key_pin;
	setKey(key_pin, "112d-rXXU-ifSr-RnXL-GbXg-8Eyf");
	CarrierKeys keys;
	deriveCarrierKeysFromPin(key_pin, keys);
	// An old numeric PIN supplies the v3, v2 and v1 keys for the mismatch cases.
	RecoveryPin old_pin;
	setLegacyPin(old_pin, 1);
	CarrierKeys old_keys;
	deriveCarrierKeysFromPin(old_pin, old_keys);
	const SensitiveU64 legacy_key(legacyKey(old_pin));

	const vBytes profile = makeTestProfile();
	auto carrier = makeTestCarrier();
	const vBytes png = embedRedditPngPayload(carrier, keys, profile);
	const auto header = headerUnder(carrier, keys.chacha);
	requireTest(header[8] == 4 && header[9] == 2, "New conceal did not emit carrier v4 / scheme 2.");
	requireTest(tryExtractPayloadWithKey(carrier, keys.chacha, 4) == std::optional<vBytes>(profile),
		"Current carrier did not decode under the Argon2-derived v4 key.");
	requireThrows([&] { (void)embedRedditPngPayload(carrier, old_keys, profile); },
		"Conceal accepted an old PIN's v3 key.");
	resetCalls();
	requireTest(recoverPayload(png, key_pin) == std::optional<vBytes>(profile),
		"Public recovery failed for carrier v4.");
	requireTest(pwhash_calls == 1 && legacy_hash_calls == 0,
		"Current carrier recovery unexpectedly attempted the legacy key.");

	RecoveryPin wrong_key;
	setKey(wrong_key, "YcVf-xkQb-6JRz-qk5k-F2tN-Lvdj");
	resetCalls();
	requireTest(!recoverPayload(png, wrong_key), "A wrong KEY returned a carrier payload.");
	requireTest(pwhash_calls == 1 && legacy_hash_calls == 0,
		"A wrong KEY bypassed Argon2 or tried the old PIN derivations.");
	resetCalls();
	requireTest(!recoverPayload(png, old_pin), "An old PIN returned a version-4 payload.");
	requireTest(pwhash_calls == 1, "An old PIN bypassed Argon2.");
	std::cout << "[PASS] emitted carrier v4 requires the KEY and rejects a wrong KEY or an old PIN\n";

	// A header of another version under each key must be an error, not a reason
	// to try the next key. Its header CRC is valid so only version binding can
	// reject these deliberately mismatched pairs.
	struct Mismatch {
		const char* what;
		vBytes png;
		bool with_key;
		unsigned legacy_calls;
	};
	const Mismatch mismatches[] = {
		{"v3 header under the v4 key", embedPngUnder(keys.chacha, profile, 3), true, 0},
		{"v2 header under the v4 key", embedPngUnder(keys.chacha, profile, 2), true, 0},
		{"v1 header under the v4 key", embedPngUnder(keys.chacha, profile, 1), true, 0},
		{"v4 header under the v3 key", embedPngUnder(old_keys.chacha, profile, 4), false, 0},
		{"v2 header under the v3 key", embedPngUnder(old_keys.chacha, profile, 2), false, 0},
		{"v1 header under the v3 key", embedPngUnder(old_keys.chacha, profile, 1), false, 0},
		{"v3 header under the v2 key", embedPngUnder(old_keys.v2.value, profile, 3), false, 0},
		{"v1 header under the v2 key", embedPngUnder(old_keys.v2.value, profile, 1), false, 0},
		{"v4 header under the legacy key", embedPngUnder(legacy_key.value, profile, 4), false, 1},
		{"v3 header under the legacy key", embedPngUnder(legacy_key.value, profile, 3), false, 1},
		{"v2 header under the legacy key", embedPngUnder(legacy_key.value, profile, 2), false, 1}
	};
	for (const auto& mismatch : mismatches) {
		resetCalls();
		requireThrows([&] { (void)recoverPayload(mismatch.png, mismatch.with_key ? key_pin : old_pin); },
			std::format("Accepted a {}.", mismatch.what));
		requireTest(pwhash_calls == 1 && legacy_hash_calls == mismatch.legacy_calls,
			std::format("A {} fell back to another key.", mismatch.what));
	}
	std::cout << "[PASS] v4, v3, v2 and legacy carrier versions are bound to their keys\n";

	auto bad_header = carrier;
	auto corrupt_header = header;
	corrupt_header[20] ^= 1U;
	embedUnder(bad_header, keys.chacha, corrupt_header);
	resetCalls();
	requireThrows([&] { (void)recoverPayload(encodeRgbPng(bad_header), key_pin); },
		"Current header CRC corruption was not reported.");
	requireTest(legacy_hash_calls == 0, "Header corruption triggered legacy fallback.");
	auto bad_payload = carrier;
	const KeyedPermutation permutation(carrier.rgb.size(), keys.chacha);
	bad_payload.rgb[permutation(HEADER_SLOTS)] ^= 1U;
	resetCalls();
	requireThrows([&] { (void)recoverPayload(encodeRgbPng(bad_payload), key_pin); },
		"Current payload CRC corruption was not reported.");
	requireTest(legacy_hash_calls == 0, "Payload corruption triggered legacy fallback.");
	std::cout << "[PASS] recognized current carrier corruption never downgrades\n";

	auto generic_carrier = makeTestCarrier();
	const vBytes generic_payload{'g', 'e', 'n', 'e', 'r', 'i', 'c'};
	const vBytes generic_png = embedRedditPngPayload(generic_carrier, keys, generic_payload);
	resetCalls();
	requireTest(!recoverPayload(generic_png, key_pin), "Generic payload was exposed as a pdvrdt profile.");
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
	// The legacy key hashes the eight PIN bytes; the v3/v4 key step hashes its
	// domain string under the Argon2 output, and the KEY check hashes 35 bytes.
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
