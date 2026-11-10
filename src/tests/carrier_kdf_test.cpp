// Include the carrier implementation so these tests can inspect the wire header
// and make deliberately invalid headers without exposing test hooks.
#include "../reddit_steg.cpp"
#include "../key_codec.h"

#include <bit>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>

namespace {

unsigned pwhash_calls = 0;
bool fail_pwhash = false;

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
}

void setKey(RecoveryKey& recovery_key, std::string_view text) {
	requireTest(parseRecoveryKeyText(text, recovery_key).has_value() && recovery_key.isKey(), "Test KEY did not parse.");
}

using ChachaKey = std::array<Byte, CarrierKey::BYTES>;

std::string hexOf(std::span<const Byte> bytes) {
	std::string hex;
	for (const Byte b : bytes) hex += std::format("{:02x}", b);
	return hex;
}

// The public recovery path as recover runs it: decode first, then the KEY.
std::optional<vBytes> recoverPayload(vBytes png, const RecoveryKey& recovery_key) {
	const auto carrier = decodeRedditRecoveryCarrier(png);
	requireTest(carrier.has_value(), "Test carrier was rejected before KEY use.");
	return extractRedditPngPayload(*carrier, recovery_key);
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


// Embed a header (and optionally a payload) under a key with any version byte
// -- the only way to build a carrier claiming another version, since conceal
// itself writes only version 4.
void embedUnder(
	RedditPngCarrier& carrier, const ChachaKey& key,
	const std::array<Byte, HEADER_SIZE>& header, std::span<const Byte> payload = {}) {
	const KeyedPermutation permutation(carrier.rgb.size(), key);
	CarrierMasks masks(key);
	masks.preparePayload(payload.size());
	// Tie-breaks only choose between equally costly changes, never what
	// decodes, so a fixed tie-break stream serves any key.
	const ChachaKey tie_key{};
	const TieBreakBits ties(tie_key, HEADER_SLOTS + payload.size() * 2U * GROUP_SAMPLES);
	const ActivitySums sums = buildActivitySums(carrier);
	const SampleCoster coster(carrier, sums);
	embedHeader(carrier, coster, permutation, masks, ties, header);
	if (!payload.empty()) {
		embedPayload(carrier, coster, permutation, masks, ties, payload);
	}
}

std::array<Byte, HEADER_SIZE> headerUnder(const RedditPngCarrier& carrier, const ChachaKey& key) {
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

vBytes embedPngUnder(const ChachaKey& key, std::span<const Byte> payload, Byte version) {
	auto carrier = makeTestCarrier();
	embedUnder(carrier, key, headerWithVersion(payload, version), payload);
	return encodeRgbPng(carrier);
}

void checkKnownAnswers() {
	// Version 4, from a 128-bit KEY. Frozen independently using Python
	// ctypes.crypto_pwhash with literal Argon2id13/2/67108864, 32 output bytes,
	// salt b"pdvrdtcarrier-v4" and the 16 raw KEY bytes as the password; then
	// hashlib.blake2b(b"pdvrdt carrier v4", key=digest, digest_size=32). The
	// KEYs are the display forms of 000102..0f, ff..ff and 00..01 (see
	// key_codec_test.cpp). Keep these literals fixed: changing a parameter or
	// byte order must break the wire-format test.
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
		RecoveryKey recovery_key;
		setKey(recovery_key, vector.key);
		resetCalls();
		CarrierKey key;
		deriveCarrierKeyFromRecoveryKey(recovery_key, key);
		requireTest(hexOf(key.bytes) == vector.v4, "Carrier v4 KDF known-answer mismatch.");
		requireTest(pwhash_calls == 1, "The carrier v4 key did not come from exactly one Argon2 run.");
	}

	RecoveryKey empty;
	CarrierKey unused;
	requireThrows([&] { deriveCarrierKeyFromRecoveryKey(empty, unused); },
		"A carrier key was derived without a recovery KEY.");
	std::cout << "[PASS] fixed v4 carrier KDF vectors; no key without a recovery KEY\n";
}

void checkPermutationIsBijection() {
	for (const std::uint64_t size : {2ULL, 3ULL, 577ULL, 4096ULL, 4097ULL, 12288ULL, 1000003ULL}) {
		const ChachaKey key{static_cast<Byte>(size), 7};
		const KeyedPermutation permutation(size, key);
		std::vector<std::size_t> all(static_cast<std::size_t>(size));
		permutation.map(0, all);
		std::vector<bool> hit(static_cast<std::size_t>(size));
		for (std::size_t i = 0; i < all.size(); ++i) {
			const std::size_t sample = all[i];
			requireTest(sample < size && !hit[sample], "Carrier permutation is not a bijection.");
			hit[sample] = true;
			if (i % 997 == 0) {
				requireTest(permutation(i) == sample,
					"A single lookup differs from the batched permutation.");
			}
		}
	}
	std::cout << "[PASS] the carrier permutation is a bijection; single and batched lookups agree\n";
}

void checkGoldenFixtures(const fs::path& tests) {
	struct Fixture {
		const char* name;
		std::string_view key;
		unsigned domain_parity;
	};
	constexpr Fixture fixtures[] = {
		{"reddit_v4_even_domain", "ERWk-iXmx-QZgW-tvfs-pSf8-Uhxi", 0},
		{"reddit_v4_odd_domain", "TEDt-XPwo-TPMi-J1Fa-MLeJ-MDei", 1}
	};
	for (const auto& fixture : fixtures) {
		vBytes png = readTestFile(tests / "golden" / fixture.name / "embedded.png");
		const auto decoded = decodeCarrier(png);
		requireTest((std::bit_width(decoded.carrier.rgb.size() - 1U) & 1U) == fixture.domain_parity,
			"Golden fixture has the wrong permutation-domain parity.");
		RecoveryKey recovery_key;
		setKey(recovery_key, fixture.key);
		CarrierKey key;
		deriveCarrierKeyFromRecoveryKey(recovery_key, key);
		const auto payload = tryExtractPayload(decoded.carrier, key.bytes);
		requireTest(payload.has_value() && isPdvrdtRedditProfile(*payload),
			"Golden fixture is not a valid carrier v4 profile.");
		resetCalls();
		const auto recovered = recoverPayload(png, recovery_key);
		requireTest(recovered == payload, "Public recovery failed for a golden Reddit fixture.");
		requireTest(pwhash_calls == 1, "Carrier recovery did not use exactly one Argon2 run.");

		fail_pwhash = true;
		resetCalls();
		requireThrows([&] { (void)recoverPayload(png, recovery_key); },
			"An Argon2 failure was swallowed during carrier recovery.");
		fail_pwhash = false;
		requireTest(pwhash_calls == 1, "An Argon2 failure was retried.");
	}
	std::cout << "[PASS] carrier v4 golden fixtures recover for both domain parities\n";
	std::cout << "[PASS] Argon2 failure propagates\n";
}

void checkCurrentCarrier() {
	RecoveryKey recovery_key;
	setKey(recovery_key, "112d-rXXU-ifSr-RnXL-GbXg-8Eyf");
	CarrierKey key;
	deriveCarrierKeyFromRecoveryKey(recovery_key, key);

	const vBytes profile = makeTestProfile();
	auto carrier = makeTestCarrier();
	const vBytes png = embedRedditPngPayload(carrier, key, profile);
	const auto header = headerUnder(carrier, key.bytes);
	requireTest(header[8] == 4 && header[9] == 2, "New conceal did not emit carrier v4 / scheme 2.");
	requireTest(tryExtractPayload(carrier, key.bytes) == std::optional<vBytes>(profile),
		"Current carrier did not decode under the Argon2-derived v4 key.");
	resetCalls();
	requireTest(recoverPayload(png, recovery_key) == std::optional<vBytes>(profile),
		"Public recovery failed for carrier v4.");
	requireTest(pwhash_calls == 1, "Current carrier recovery did not use exactly one Argon2 run.");

	RecoveryKey wrong_key;
	setKey(wrong_key, "YcVf-xkQb-6JRz-qk5k-F2tN-Lvdj");
	resetCalls();
	requireTest(!recoverPayload(png, wrong_key), "A wrong KEY returned a carrier payload.");
	requireTest(pwhash_calls == 1, "A wrong KEY bypassed Argon2.");
	std::cout << "[PASS] emitted carrier v4 requires its KEY and rejects a wrong KEY\n";

	// A header found under the KEY but naming another version (the CRC is
	// valid, so only the version byte rejects it) is an unsupported format,
	// never a payload.
	for (const Byte version : {Byte{1}, Byte{2}, Byte{3}, Byte{5}}) {
		const vBytes other = embedPngUnder(key.bytes, profile, version);
		resetCalls();
		requireThrows([&] { (void)recoverPayload(other, recovery_key); },
			std::format("Accepted a version-{} header.", version));
		requireTest(pwhash_calls == 1, "A foreign header version was retried.");
	}
	std::cout << "[PASS] carrier headers of any version but 4 are refused\n";

	auto bad_header = carrier;
	auto corrupt_header = header;
	corrupt_header[20] ^= 1U;
	embedUnder(bad_header, key.bytes, corrupt_header);
	requireThrows([&] { (void)recoverPayload(encodeRgbPng(bad_header), recovery_key); },
		"Current header CRC corruption was not reported.");
	auto bad_payload = carrier;
	const KeyedPermutation permutation(carrier.rgb.size(), key.bytes);
	bad_payload.rgb[permutation(HEADER_SLOTS)] ^= 1U;
	requireThrows([&] { (void)recoverPayload(encodeRgbPng(bad_payload), recovery_key); },
		"Current payload CRC corruption was not reported.");
	std::cout << "[PASS] header and payload corruption are reported\n";

	auto generic_carrier = makeTestCarrier();
	const vBytes generic_payload{'g', 'e', 'n', 'e', 'r', 'i', 'c'};
	const vBytes generic_png = embedRedditPngPayload(generic_carrier, key, generic_payload);
	requireTest(!recoverPayload(generic_png, recovery_key), "Generic payload was exposed as a pdvrdt profile.");
	std::cout << "[PASS] a valid generic carrier returns no profile\n";
}

// A deterministic, odd-sized, noisy cover: edges on every side and enough
// texture for the costing's tie-breaks to run.
RedditPngCarrier pinnedCarrier(std::uint32_t width, std::uint32_t height) {
	std::uint32_t state = 0x12345678u;
	RedditPngCarrier carrier{.width = width, .height = height};
	for (std::uint32_t y = 0; y < height; ++y) {
		for (std::uint32_t x = 0; x < width; ++x) {
			for (std::uint32_t channel = 0; channel < 3; ++channel) {
				state = state * 1664525u + 1013904223u;
				const std::uint32_t base = (x * 2 + y * 3 + channel * 40) & 0xffu;
				carrier.rgb.push_back(static_cast<Byte>((base + (state >> 28)) & 0xffu));
			}
		}
	}
	carrier.payload_capacity = theoreticalCapacity(carrier.rgb.size());
	return carrier;
}

std::uint64_t fnv1a(std::span<const Byte> bytes) {
	std::uint64_t hash = 0xcbf29ce484222325ULL;
	for (const Byte b : bytes) {
		hash = (hash ^ b) * 0x100000001b3ULL;
	}
	return hash;
}

// The changed samples are a function of the cover, key and payload alone --
// not of the thread count, nor of which compressor wrote the PNG. Pin them on
// a small cover (the value the one-group-at-a-time algorithm has always
// produced; the Rust port pins the same, in embedding_output_is_pinned), and
// check that a near-capacity payload, where most groups are re-costed after
// speculation, embeds identically on every thread count.
void checkPinnedEmbedding() {
	CarrierKey key;
	for (std::size_t i = 0; i < key.bytes.size(); ++i) {
		key.bytes[i] = static_cast<Byte>(i * 7 + 3);
	}
	const auto payloadOf = [](std::size_t size) {
		vBytes payload;
		for (std::uint32_t i = 0; i < size; ++i) {
			payload.push_back(static_cast<Byte>((i * 2654435761u) >> 24));
		}
		return payload;
	};

	struct Case {
		std::uint32_t width, height;
		std::size_t payload_size;
		std::uint64_t expected;  // 0: the one-thread result
	};
	const Case cases[] = {
		{97, 61, 300, 0xef67b0a7233a2a17ULL},
		{97, 61, 570, 0},       // near capacity
		{613, 407, 20000, 0},   // many chunks
	};
	for (const Case& test : cases) {
		const vBytes payload = payloadOf(test.payload_size);
		std::uint64_t serial = 0;
		for (const unsigned threads : {1U, 2U, 3U, 7U, 16U}) {
			RedditPngCarrier carrier = pinnedCarrier(test.width, test.height);
			const vBytes png = embedRedditPngPayloadOn(carrier, key, payload, threads);
			const std::uint64_t hash = fnv1a(carrier.rgb);
			if (threads == 1) {
				serial = hash;
				requireTest(test.expected == 0 || hash == test.expected,
					std::format("Pinned embedding changed: {:016x}.", hash));
			}
			requireTest(hash == serial,
				std::format("{}x{}: {} threads embedded differently from one.", test.width, test.height, threads));
			vBytes copy = png;
			const auto decoded = decodeRedditRecoveryCarrier(copy);
			requireTest(decoded && decoded->rgb == carrier.rgb,
				"Encoded carrier does not decode to its pixels.");
			requireTest(tryExtractPayload(*decoded, key.bytes, threads) == std::optional<vBytes>(payload),
				"Carrier did not recover its payload.");
		}
	}
	std::cout << "[PASS] embedding is pinned and identical on 1, 2, 3, 7 and 16 threads; each recovers\n";

	// Both compressors (libdeflate on one thread; sliced zlib on 16 for this
	// 3.4 MiB stream, see useSlicedZlib) must write PNGs that decode to the same
	// pixels, including across slice seams.
	RedditPngCarrier large = pinnedCarrier(1201, 977);
	for (const unsigned threads : {1U, 16U}) {
		vBytes png = encodeRgbPng(large, threads);
		const auto decoded = decodeRedditRecoveryCarrier(png);
		requireTest(decoded && decoded->rgb == large.rgb,
			std::format("PNG written on {} threads does not decode to its pixels.", threads));
	}
	std::cout << "[PASS] single and sliced zlib PNG streams decode to the same pixels\n";

	// Slicing only where it measured faster: the busiest thread's share must be
	// under 2/5 of the stream. Filtered RGB is (3 * width + 1) * height bytes.
	const auto filtered = [](std::size_t side) { return (3 * side + 1) * side; };
	requireTest(!useSlicedZlib(filtered(592), 16) && !useSlicedZlib(filtered(900), 16) &&
		useSlicedZlib(filtered(1000), 16) && useSlicedZlib(filtered(1200), 4) &&
		!useSlicedZlib(filtered(1200), 3) && useSlicedZlib(filtered(2000), 3) &&
		!useSlicedZlib(filtered(3000), 2) && !useSlicedZlib(filtered(3000), 1),
		"Sliced-zlib choice does not follow the measured crossover.");
	std::cout << "[PASS] sliced zlib is chosen only past the measured crossover\n";
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

int main(int argc, char** argv) {
	try {
		if (argc != 2 || sodium_init() < 0) return 2;
		checkKnownAnswers();
		checkPermutationIsBijection();
		checkGoldenFixtures(fs::path(argv[1]));
		checkCurrentCarrier();
		checkPinnedEmbedding();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "[FAIL] " << error.what() << '\n';
		return 1;
	}
}
