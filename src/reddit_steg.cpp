#include "reddit_steg.h"

#include "encryption.h"
#include "io_utils.h"
#include "lodepng/lodepng_config.h"
#include "lodepng/lodepng.h"
#include "lodepng/lodepng_zlib_adapter.h"
#include "png_utils.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

// Carrier keying.
//
// Every sample position and whitening bit derives from a carrier key, so
// finding the known header under a candidate key confirms that an image carries
// a payload. Version 3, the only one conceal writes, keys ChaCha20 with a
// 256-bit key from Argon2id: the Feistel round functions are tables of ChaCha20
// output and the whitening bits are ChaCha20 keystream, so there is no cheaper
// way to test a key than the Argon2 PIN derivation itself. Versions 1 and 2 used
// splitmix64 under a 64-bit key -- fast, but not a cryptographic function, so a
// direct search of that key space bypassed Argon2. They are retained only for
// recovery, version 1 with its cheap PIN hash.
//
// The header version is checked against the derivation used to find it; a
// lookup under one version's key never accepts another version's header.
// Payload confidentiality is provided by secretstream with its separate,
// randomly salted Argon2id key.
constexpr std::uint64_t
	PERMUTATION_DOMAIN = 0x9f6abc142d337e51ULL,
	HEADER_DOMAIN      = 0x684452c3a109f5d7ULL,
	PAYLOAD_DOMAIN     = 0x5041594c4f414431ULL;

constexpr auto CARRIER_MAGIC = std::to_array<Byte>({
	'P', 'N', 'G', 'S', 'T', 'E', 'G', '1'
});

constexpr Byte
	CARRIER_VERSION = 3,
	V2_CARRIER_VERSION = 2,      // recovery only
	LEGACY_CARRIER_VERSION = 1,  // recovery only
	// Scheme 2: KeyedPermutation uses the next power-of-two domain rather than
	// the next even-power one.
	//
	// Where the cover's sample count has an even bit length the permutation is
	// unchanged, so a scheme-1 image still decodes here and is then rejected by
	// validateCarrierHeader() with a clear "unsupported carrier format". Where it
	// is odd every carrier position moves, the header does not decode at all, and
	// the image is indistinguishable from one holding no payload. Scheme 1 images
	// from odd-bit-length covers are therefore not recoverable by this build.
	CARRIER_SCHEME  = 2;

// GROUP_SAMPLES = 15 carrier samples per 4-bit nibble, with at most one sample
// changed -- the F5/matrix-embedding reading of "(15,4)" (n samples, k bits),
// not the coding-theory (n,k) for the underlying Hamming code, which would be
// written (15,11). The syndrome is the XOR of the 1-based positions of the odd
// samples, so moving it to any target costs a single flip.
constexpr std::size_t
	HEADER_COPIES = 3,
	HEADER_SIZE   = 24,
	HEADER_BITS   = HEADER_SIZE * 8,
	HEADER_SLOTS  = HEADER_BITS * HEADER_COPIES,
	GROUP_SAMPLES = 15;

struct ChangeChoice {
	double cost{};
	Byte value{};
};

[[nodiscard]] constexpr std::uint64_t splitmix64(std::uint64_t value) noexcept {
	value += 0x9e3779b97f4a7c15ULL;
	value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
	value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
	return value ^ (value >> 31U);
}

void writeLe32(Byte* data, std::uint32_t value) noexcept {
	data[0] = static_cast<Byte>(value);
	data[1] = static_cast<Byte>(value >> 8U);
	data[2] = static_cast<Byte>(value >> 16U);
	data[3] = static_cast<Byte>(value >> 24U);
}

[[nodiscard]] std::uint32_t readLe32(const Byte* data) noexcept {
	return
		static_cast<std::uint32_t>(data[0]) |
		(static_cast<std::uint32_t>(data[1]) << 8U) |
		(static_cast<std::uint32_t>(data[2]) << 16U) |
		(static_cast<std::uint32_t>(data[3]) << 24U);
}

// Version 3 keystreams: separate ChaCha20 (IETF, RFC 8439) streams under the
// version-3 key, told apart by their 96-bit nonce, each starting at block 0.
enum class V3Stream : Byte { permutation = 1, header = 2, payload = 3, tie_break = 4 };

void chachaKeystream(
	std::span<const Byte, CarrierKeys::V3_KEY_BYTES> key,
	V3Stream stream,
	std::span<Byte> out) {
	if (out.empty()) {
		return;  // an empty span may carry a null pointer, which libsodium rejects
	}
	const std::array<Byte, crypto_stream_chacha20_ietf_NONCEBYTES> nonce{
		'P', 'D', 'V', 'R', 'D', 'T', 'v', '3', static_cast<Byte>(stream), 0, 0, 0
	};
	if (crypto_stream_chacha20_ietf(out.data(), out.size(), nonce.data(), key.data()) != 0) {
		throw std::runtime_error("Internal Error: Reddit carrier keystream failed.");
	}
}

// Eight-round Feistel permutation over the smallest power-of-two domain
// containing the sample count. Cycle walking restricts that permutation to the
// exact RGB-sample range without allocating a carrier-index array.
//
// The halves are allowed to differ in width by one bit, so the domain is always
// the next power of two rather than the next *even* power. Rounding the bit
// count up to an even number (scheme 1) doubled the domain again whenever that
// count was odd, and cycle walking then ran ~4 iterations per lookup instead of
// ~2 -- a cliff an image could fall off by being one pixel wider.
//
// An odd bit count makes the two halves unequal, so their widths swap on every
// round: even rounds take the right_bits_-wide half and produce a
// left_bits_-wide one, odd rounds the reverse. The round count is even, which
// puts them back. The round is still invertible (right is carried across
// unchanged, left is recovered by XORing the same round function), so this
// remains a permutation of the whole domain. Where the bit count is even the
// halves are equal and the swap is a no-op.
//
// The round function is the only difference between carrier versions: splitmix64
// under a 64-bit key for versions 1 and 2, a table of ChaCha20 output for 3.
class KeyedPermutation {
public:
	static constexpr std::size_t ROUNDS = 8;

	// Versions 1 and 2. Round keys are derived once here: they depend only on
	// the round index and the carrier key.
	KeyedPermutation(std::uint64_t size, std::uint64_t carrier_key) : size_(size) {
		initGeometry();
		for (std::size_t round = 0; round < ROUNDS; ++round) {
			round_keys_[round] = splitmix64(
				carrier_key ^ PERMUTATION_DOMAIN ^
				(static_cast<std::uint64_t>(round) * 0x9e3779b97f4a7c15ULL));
		}
	}

	// Version 3. Each round function is a table with one entry per possible
	// input half, filled from ChaCha20 -- a keyed random function rather than a
	// fast mixer of a small key. Round r's table occupies entries
	// [r * 2^right_bits_, (r + 1) * 2^right_bits_) of one keystream, read as
	// little-endian 32-bit words masked to the round's output width; odd rounds,
	// whose input is only left_bits_ wide, use the first 2^left_bits_ of theirs.
	// At the largest cover accepted that is 8 * 2^14 words: 512 KiB, filled by
	// one bulk keystream call.
	KeyedPermutation(std::uint64_t size, std::span<const Byte, CarrierKeys::V3_KEY_BYTES> key)
		: size_(size) {
		initGeometry();
		table_stride_ = std::size_t{1} << right_bits_;
		tables_.resize(ROUNDS * table_stride_);
		vBytes keystream(tables_.size() * 4U);
		ScopedWipe keystream_wipe{keystream};
		chachaKeystream(key, V3Stream::permutation, keystream);
		for (std::size_t round = 0; round < ROUNDS; ++round) {
			const auto out_mask = static_cast<std::uint32_t>(roundMask(round));
			for (std::size_t i = 0; i < table_stride_; ++i) {
				const std::size_t entry = round * table_stride_ + i;
				tables_[entry] = readLe32(keystream.data() + entry * 4U) & out_mask;
			}
		}
	}

	// The round keys and tables are as good as the carrier key: either locates
	// the payload.
	~KeyedPermutation() {
		sodium_memzero(round_keys_.data(), sizeof(round_keys_));
		if (!tables_.empty()) {
			sodium_memzero(tables_.data(), tables_.size() * sizeof(tables_[0]));
		}
	}
	KeyedPermutation(const KeyedPermutation&) = delete;
	KeyedPermutation& operator=(const KeyedPermutation&) = delete;

	[[nodiscard]] std::size_t operator()(std::uint64_t ordinal) const {
		std::size_t sample = 0;
		map(ordinal, std::span(&sample, 1));
		return sample;
	}

	// out[i] = permutation(first + i). Batched because a single lookup is a
	// chain of eight dependent rounds ended by an unpredictable cycle-walking
	// branch, which serialises the pipeline: here the rounds run branch-free
	// across a block, so independent lookups overlap, and only values still
	// outside the domain go round again. Measured ~62 -> ~30 ns per lookup for
	// splitmix64. Every value is exactly what a single lookup returns.
	void map(std::uint64_t first, std::span<std::size_t> out) const {
		if (first > size_ || out.size() > size_ - first) {
			throw std::runtime_error("Internal Error: Reddit carrier index is out of range.");
		}
		if (tables_.empty()) {
			mapBlocks<false>(first, out);
		} else {
			mapBlocks<true>(first, out);
		}
	}

private:
	void initGeometry() {
		if (size_ < 2) {
			throw std::runtime_error("Internal Error: Reddit carrier sample domain is too small.");
		}
		const auto bits = static_cast<unsigned>(std::bit_width(size_ - 1U));
		left_bits_  = bits / 2U;
		right_bits_ = bits - left_bits_;
		left_mask_  = (std::uint64_t{1} << left_bits_) - 1U;
		right_mask_ = (std::uint64_t{1} << right_bits_) - 1U;
	}

	[[nodiscard]] std::uint64_t roundMask(std::size_t round) const noexcept {
		return (round % 2U == 0U) ? left_mask_ : right_mask_;
	}

	template <bool Tables>
	[[nodiscard]] std::uint64_t permutePowerOfTwo(std::uint64_t value) const noexcept {
		std::uint64_t left = value >> right_bits_;
		std::uint64_t right = value & right_mask_;
		for (std::size_t round = 0; round < ROUNDS; ++round) {
			std::uint64_t function;
			if constexpr (Tables) {
				function = tables_[round * table_stride_ + right];
			} else {
				function = splitmix64(right ^ round_keys_[round]);
			}
			const std::uint64_t next_right = (left ^ function) & roundMask(round);
			left = right;
			right = next_right;
		}
		return (left << right_bits_) | right;
	}

	template <bool Tables>
	void mapBlocks(std::uint64_t first, std::span<std::size_t> out) const {
		constexpr std::size_t BLOCK = 64;
		std::array<std::uint64_t, BLOCK> values{};
		std::array<std::uint32_t, BLOCK> slots{};
		for (std::size_t base = 0; base < out.size(); base += BLOCK) {
			std::size_t pending = std::min(BLOCK, out.size() - base);
			for (std::size_t i = 0; i < pending; ++i) {
				values[i] = first + base + i;
				slots[i] = static_cast<std::uint32_t>(i);
			}
			while (pending != 0) {
				for (std::size_t i = 0; i < pending; ++i) {
					values[i] = permutePowerOfTwo<Tables>(values[i]);
				}
				// Branch-free compaction: every value is written to its slot, and
				// one still outside the domain is kept for another pass, whose
				// result then overwrites it.
				std::size_t kept = 0;
				for (std::size_t i = 0; i < pending; ++i) {
					const std::uint64_t value = values[i];
					out[base + slots[i]] = static_cast<std::size_t>(value);
					values[kept] = value;
					slots[kept] = slots[i];
					kept += static_cast<std::size_t>(value >= size_);
				}
				pending = kept;
			}
		}
	}

	std::array<std::uint64_t, ROUNDS> round_keys_{};  // versions 1 and 2
	std::vector<std::uint32_t> tables_{};             // version 3
	std::size_t table_stride_{};
	std::uint64_t size_{};
	std::uint64_t left_mask_{};
	std::uint64_t right_mask_{};
	unsigned left_bits_{};
	unsigned right_bits_{};

	static_assert(ROUNDS % 2 == 0,
		"The half widths swap every round; an even round count is what puts them "
		"back before the halves are recombined.");
};

// Header and payload whitening. Versions 1 and 2 compute each mask from the
// 64-bit key with splitmix64; version 3 XORs the header with 24 keystream bytes
// (bit i is bit 7 - i % 8 of byte i / 8) and each payload nibble with the
// matching nibble of a keystream as long as the payload.
class CarrierMasks {
public:
	explicit CarrierMasks(std::uint64_t key) : key64_(key) {}

	explicit CarrierMasks(std::span<const Byte, CarrierKeys::V3_KEY_BYTES> key) : v3_(true) {
		std::copy(key.begin(), key.end(), key_.begin());
		chachaKeystream(key_, V3Stream::header, header_);
	}

	~CarrierMasks() {
		sodium_memzero(&key64_, sizeof(key64_));
		sodium_memzero(key_.data(), key_.size());
		sodium_memzero(header_.data(), header_.size());
		if (!payload_.empty()) {
			sodium_memzero(payload_.data(), payload_.size());
		}
	}
	CarrierMasks(const CarrierMasks&) = delete;
	CarrierMasks& operator=(const CarrierMasks&) = delete;

	// Version 3 sizes its payload keystream once the payload length is known.
	void preparePayload(std::size_t payload_size) {
		if (v3_) {
			payload_.resize(payload_size);
			chachaKeystream(key_, V3Stream::payload, payload_);
		}
	}

	[[nodiscard]] unsigned header(std::size_t bit_index) const noexcept {
		if (v3_) {
			return (header_[bit_index / 8U] >> (7U - (bit_index % 8U))) & 1U;
		}
		return static_cast<unsigned>(
			splitmix64(
				key64_ ^ HEADER_DOMAIN ^
				(static_cast<std::uint64_t>(bit_index) * 0x9e3779b97f4a7c15ULL)) &
			1U);
	}

	[[nodiscard]] unsigned payload(std::size_t group_index) const noexcept {
		if (v3_) {
			const Byte mask = payload_[group_index / 2U];
			return (group_index & 1U) == 0U ? mask >> 4U : mask & 0x0fU;
		}
		return static_cast<unsigned>(
			splitmix64(
				key64_ ^ PAYLOAD_DOMAIN ^
				(static_cast<std::uint64_t>(group_index) * 0x9e3779b97f4a7c15ULL)) &
			0x0fU);
	}

private:
	bool v3_{false};
	std::uint64_t key64_{};
	std::array<Byte, CarrierKeys::V3_KEY_BYTES> key_{};
	std::array<Byte, HEADER_SIZE> header_{};
	vBytes payload_{};
};

// Conceal's tie-break between equally costly +1 and -1 changes. Recovery never
// reads it, so it only needs to be unbiased; it is indexed by carrier ordinal,
// the order samples are visited, so it is read sequentially rather than missing
// cache on every tie as a per-sample table did.
class TieBreakBits {
public:
	TieBreakBits(std::span<const Byte, CarrierKeys::V3_KEY_BYTES> key, std::size_t ordinals)
		: bits_((ordinals + 7U) / 8U) {
		chachaKeystream(key, V3Stream::tie_break, bits_);
	}
	~TieBreakBits() {
		if (!bits_.empty()) {
			sodium_memzero(bits_.data(), bits_.size());
		}
	}
	TieBreakBits(const TieBreakBits&) = delete;
	TieBreakBits& operator=(const TieBreakBits&) = delete;

	[[nodiscard]] bool takeHigher(std::size_t ordinal) const noexcept {
		return ((bits_[ordinal / 8U] >> (ordinal % 8U)) & 1U) != 0U;
	}

private:
	vBytes bits_;
};

[[nodiscard]] std::size_t theoreticalCapacity(std::size_t rgb_samples) noexcept {
	if (rgb_samples <= HEADER_SLOTS) {
		return 0;
	}
	const std::size_t groups = (rgb_samples - HEADER_SLOTS) / GROUP_SAMPLES;
	return groups / 2U;
}

[[nodiscard]] std::array<Byte, HEADER_SIZE> makeHeader(std::span<const Byte> payload) {
	if (payload.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
		throw std::runtime_error("Data File Size Error: Reddit carrier payload exceeds its format limit.");
	}

	std::array<Byte, HEADER_SIZE> header{};
	std::copy(CARRIER_MAGIC.begin(), CARRIER_MAGIC.end(), header.begin());
	header[8] = CARRIER_VERSION;
	header[9] = CARRIER_SCHEME;
	header[10] = static_cast<Byte>(HEADER_COPIES);
	header[11] = 0;
	writeLe32(header.data() + 12, static_cast<std::uint32_t>(payload.size()));
	writeLe32(header.data() + 16, pdvrdtCrc32Update(0, payload));
	writeLe32(
		header.data() + 20,
		pdvrdtCrc32Update(0, std::span<const Byte>(header).first(20)));
	return header;
}

[[nodiscard]] unsigned headerBit(
	const std::array<Byte, HEADER_SIZE>& header,
	std::size_t bit_index) noexcept {

	return (header[bit_index / 8U] >> (7U - (bit_index % 8U))) & 1U;
}

void setHeaderBit(
	std::array<Byte, HEADER_SIZE>& header,
	std::size_t bit_index,
	unsigned bit) noexcept {

	const Byte mask = static_cast<Byte>(1U << (7U - (bit_index % 8U)));
	if (bit != 0U) {
		header[bit_index / 8U] |= mask;
	} else {
		header[bit_index / 8U] &= static_cast<Byte>(~mask);
	}
}

[[nodiscard]] unsigned luminance(std::span<const Byte> rgb, std::size_t pixel) noexcept {
	const std::size_t offset = pixel * 3U;
	return (
		77U * rgb[offset] +
		150U * rgb[offset + 1U] +
		29U * rgb[offset + 2U] +
		128U) >> 8U;
}

[[nodiscard]] vBytes buildActivityMap(const RedditPngCarrier& carrier) {
	if (carrier.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
		carrier.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
		throw std::runtime_error("Image Size Error: Reddit carrier dimensions are too large.");
	}
	const int width = static_cast<int>(carrier.width);
	const int height = static_cast<int>(carrier.height);
	const std::size_t pixels = checkedMulSize(
		static_cast<std::size_t>(carrier.width),
		static_cast<std::size_t>(carrier.height),
		"Image Size Error: Reddit carrier pixel count overflow.");
	// Each pixel's luminance is read by its own cell and by the eight around it,
	// so computing it on demand evaluated the same three multiplies nine times
	// over. One byte per pixel of scratch removes that: measured 155ms -> 99ms on
	// a 9-megapixel cover, with a bit-identical activity map.
	vBytes luma(pixels);
	for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
		luma[pixel] = static_cast<Byte>(luminance(carrier.rgb, pixel));
	}

	vBytes activity(pixels);

	const auto y_at = [&](int x, int y) {
		x = std::clamp(x, 0, width - 1);
		y = std::clamp(y, 0, height - 1);
		const std::size_t pixel =
			static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
			static_cast<std::size_t>(x);
		return static_cast<int>(luma[pixel]);
	};
	const auto directional_score = [](int center, int first, int second) static {
		return (
			std::abs(center - first) +
			std::abs(center - second) +
			std::abs(first - second)) / 3;
	};

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const int center = y_at(x, y);
			const int horizontal =
				directional_score(center, y_at(x - 1, y), y_at(x + 1, y));
			const int vertical =
				directional_score(center, y_at(x, y - 1), y_at(x, y + 1));
			const int diagonal_down =
				directional_score(center, y_at(x - 1, y - 1), y_at(x + 1, y + 1));
			const int diagonal_up =
				directional_score(center, y_at(x + 1, y - 1), y_at(x - 1, y + 1));
			const int least_directional_activity =
				std::min({horizontal, vertical, diagonal_down, diagonal_up, 255});
			activity[
				static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
				static_cast<std::size_t>(x)] =
				static_cast<Byte>(least_directional_activity);
		}
	}
	return activity;
}

[[nodiscard]] double smoothedActivity(
	std::span<const Byte> activity,
	std::uint32_t width,
	std::uint32_t height,
	std::size_t pixel) {

	const int x = static_cast<int>(pixel % width);
	const int y = static_cast<int>(pixel / width);
	unsigned total = 0;
	unsigned count = 0;
	for (int dy = -1; dy <= 1; ++dy) {
		const int neighbor_y = std::clamp(
			y + dy, 0, static_cast<int>(height) - 1);
		for (int dx = -1; dx <= 1; ++dx) {
			const int neighbor_x = std::clamp(
				x + dx, 0, static_cast<int>(width) - 1);
			total += activity[
				static_cast<std::size_t>(neighbor_y) * width +
				static_cast<std::size_t>(neighbor_x)];
			++count;
		}
	}
	return static_cast<double>(total) / static_cast<double>(count);
}

[[nodiscard]] unsigned neighborDifference(
	const RedditPngCarrier& carrier,
	std::size_t sample,
	Byte candidate) {

	const std::size_t pixel = sample / 3U;
	const std::size_t channel = sample % 3U;
	const int x = static_cast<int>(pixel % carrier.width);
	const int y = static_cast<int>(pixel / carrier.width);
	unsigned total = 0;
	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			if (dx == 0 && dy == 0) {
				continue;
			}
			const int neighbor_x = std::clamp(
				x + dx, 0, static_cast<int>(carrier.width) - 1);
			const int neighbor_y = std::clamp(
				y + dy, 0, static_cast<int>(carrier.height) - 1);
			const std::size_t neighbor_pixel =
				static_cast<std::size_t>(neighbor_y) * carrier.width +
				static_cast<std::size_t>(neighbor_x);
			const Byte neighbor = carrier.rgb[neighbor_pixel * 3U + channel];
			total += static_cast<unsigned>(std::abs(
				static_cast<int>(candidate) - static_cast<int>(neighbor)));
		}
	}
	return total;
}

[[nodiscard]] ChangeChoice chooseChange(
	const RedditPngCarrier& carrier,
	std::span<const Byte> activity,
	bool tie_takes_higher,
	std::size_t sample) {

	const Byte original = carrier.rgb[sample];
	const std::size_t pixel = sample / 3U;
	const std::size_t channel = sample % 3U;
	const double local_activity =
		smoothedActivity(activity, carrier.width, carrier.height, pixel);
	constexpr std::array<double, 3> CHANNEL_WEIGHT = {1.05, 1.25, 0.90};
	const double base_cost =
		(1.0 + 256.0 / (local_activity + 4.0)) * CHANNEL_WEIGHT[channel];

	std::array<Byte, 2> candidates{};
	std::size_t candidate_count = 0;
	if (original > 0) {
		candidates[candidate_count++] = static_cast<Byte>(original - 1U);
	}
	if (original < 255) {
		candidates[candidate_count++] = static_cast<Byte>(original + 1U);
	}

	ChangeChoice best{
		.cost = std::numeric_limits<double>::infinity(),
		.value = original
	};
	for (std::size_t i = 0; i < candidate_count; ++i) {
		const Byte candidate = candidates[i];
		const double cost =
			base_cost +
			static_cast<double>(neighborDifference(carrier, sample, candidate)) / 512.0;
		if (cost < best.cost) {
			best = {.cost = cost, .value = candidate};
		} else if (cost == best.cost && tie_takes_higher) {
			best = {.cost = cost, .value = candidate};
		}
	}
	return best;
}

void embedHeader(
	RedditPngCarrier& carrier,
	std::span<const Byte> activity,
	const KeyedPermutation& permutation,
	const CarrierMasks& masks,
	const TieBreakBits& ties,
	const std::array<Byte, HEADER_SIZE>& header) {

	std::array<std::size_t, HEADER_SLOTS> samples{};
	permutation.map(0, samples);
	for (std::size_t bit = 0; bit < HEADER_BITS; ++bit) {
		const unsigned target = headerBit(header, bit) ^ masks.header(bit);
		for (std::size_t copy = 0; copy < HEADER_COPIES; ++copy) {
			const std::size_t ordinal = bit * HEADER_COPIES + copy;
			const std::size_t sample = samples[ordinal];
			if ((carrier.rgb[sample] & 1U) != target) {
				carrier.rgb[sample] =
					chooseChange(carrier, activity, ties.takeHigher(ordinal), sample).value;
			}
		}
	}
}

// Groups whose sample positions are resolved per batched permutation call.
constexpr std::size_t GROUPS_PER_BATCH = 256;

void prefetchGroup(
	const RedditPngCarrier& carrier,
	std::span<const Byte> activity,
	std::span<const std::size_t> samples) noexcept {

	const std::size_t row_bytes = static_cast<std::size_t>(carrier.width) * 3U;
	for (const std::size_t sample : samples) {
		const char* const base = reinterpret_cast<const char*>(carrier.rgb.data()) + sample;
		__builtin_prefetch(base, 0, 1);
		if (sample >= row_bytes) {
			__builtin_prefetch(base - row_bytes, 0, 1);
		}
		if (sample + row_bytes < carrier.rgb.size()) {
			__builtin_prefetch(base + row_bytes, 0, 1);
		}
		__builtin_prefetch(activity.data() + (sample / 3U), 0, 1);
	}
}

void embedPayload(
	RedditPngCarrier& carrier,
	std::span<const Byte> activity,
	const KeyedPermutation& permutation,
	const CarrierMasks& masks,
	const TieBreakBits& ties,
	std::span<const Byte> payload) {

	const std::size_t nibble_count = checkedMulSize(
		payload.size(), 2, "Data File Size Error: Reddit payload size overflow.");

	// This loop is memory-bound, not arithmetic-bound. The permutation
	// deliberately scatters a group's 15 samples across the whole image, so on
	// any cover larger than cache each chooseChange() below stalls on cold lines.
	// Positions are resolved a batch at a time, and the next group's pixel
	// neighbourhoods are prefetched while this one is costed, so those misses
	// overlap instead of serialising.
	//
	// Things that were tried here and did NOT help, so they are not worth
	// re-attempting: precomputing the 3x3 smoothed-activity map once per pixel
	// rather than once per channel, and deferring the 15 chooseChange() calls
	// until delta is known to be non-zero (one group in sixteen discards all
	// 15). Both are output-preserving and both measured as no change -- the
	// arithmetic they remove is not the bottleneck.
	std::vector<std::size_t> positions(
		std::min(nibble_count, GROUPS_PER_BATCH) * GROUP_SAMPLES);
	for (std::size_t batch_start = 0; batch_start < nibble_count; batch_start += GROUPS_PER_BATCH) {
		const std::size_t batch_groups = std::min(GROUPS_PER_BATCH, nibble_count - batch_start);
		const std::span<const std::size_t> batch =
			std::span(positions).first(batch_groups * GROUP_SAMPLES);
		permutation.map(
			HEADER_SLOTS + batch_start * GROUP_SAMPLES,
			std::span(positions).first(batch.size()));
		prefetchGroup(carrier, activity, batch.first(GROUP_SAMPLES));

		for (std::size_t in_batch = 0; in_batch < batch_groups; ++in_batch) {
			const std::size_t group = batch_start + in_batch;
			const std::size_t first_ordinal = HEADER_SLOTS + group * GROUP_SAMPLES;
			const auto samples = batch.subspan(in_batch * GROUP_SAMPLES, GROUP_SAMPLES);
			if (in_batch + 1U < batch_groups) {
				prefetchGroup(carrier, activity, batch.subspan((in_batch + 1U) * GROUP_SAMPLES, GROUP_SAMPLES));
			}

			std::array<ChangeChoice, GROUP_SAMPLES> choices{};
			unsigned syndrome = 0;
			for (std::size_t position = 0; position < GROUP_SAMPLES; ++position) {
				if ((carrier.rgb[samples[position]] & 1U) != 0U) {
					syndrome ^= static_cast<unsigned>(position + 1U);
				}
				choices[position] = chooseChange(
					carrier, activity, ties.takeHigher(first_ordinal + position), samples[position]);
			}

			const unsigned nibble = (group & 1U) == 0U
				? payload[group / 2U] >> 4U
				: payload[group / 2U] & 0x0fU;
			const unsigned delta = syndrome ^ nibble ^ masks.payload(group);
			if (delta == 0U) {
				continue;
			}

			const std::size_t single = static_cast<std::size_t>(delta - 1U);
			double best_cost = choices[single].cost;
			std::size_t first = single;
			std::size_t second = GROUP_SAMPLES;

			for (unsigned first_index = 1; first_index <= 15; ++first_index) {
				const unsigned second_index = first_index ^ delta;
				if (second_index < 1U || second_index > 15U || first_index >= second_index) {
					continue;
				}
				const double pair_cost =
					choices[first_index - 1U].cost +
					choices[second_index - 1U].cost;
				if (pair_cost < best_cost) {
					best_cost = pair_cost;
					first = first_index - 1U;
					second = second_index - 1U;
				}
			}

			carrier.rgb[samples[first]] = choices[first].value;
			if (second != GROUP_SAMPLES) {
				carrier.rgb[samples[second]] = choices[second].value;
			}
		}
	}
}

[[nodiscard]] std::array<Byte, HEADER_SIZE> extractHeader(
	const RedditPngCarrier& carrier,
	const KeyedPermutation& permutation,
	const CarrierMasks& masks) {

	std::array<std::size_t, HEADER_SLOTS> samples{};
	permutation.map(0, samples);
	std::array<Byte, HEADER_SIZE> header{};
	for (std::size_t bit = 0; bit < HEADER_BITS; ++bit) {
		unsigned ones = 0;
		for (std::size_t copy = 0; copy < HEADER_COPIES; ++copy) {
			ones += carrier.rgb[samples[bit * HEADER_COPIES + copy]] & 1U;
		}
		const unsigned whitened = ones > HEADER_COPIES / 2U ? 1U : 0U;
		setHeaderBit(header, bit, whitened ^ masks.header(bit));
	}
	return header;
}

void validateCarrierHeader(
	const std::array<Byte, HEADER_SIZE>& header,
	std::size_t payload_capacity,
	Byte expected_version) {

	constexpr std::string_view CORRUPT_HEADER =
		"File Recovery Error: Reddit carrier header is corrupt.";
	if (header[8] != expected_version ||
		header[9] != CARRIER_SCHEME ||
		header[10] != static_cast<Byte>(HEADER_COPIES) ||
		header[11] != 0) {
		throw std::runtime_error(
			"File Recovery Error: Unsupported Reddit carrier format.");
	}

	const std::uint32_t expected_crc = readLe32(header.data() + 20);
	const std::uint32_t actual_crc =
		pdvrdtCrc32Update(0, std::span<const Byte>(header).first(20));
	if (expected_crc != actual_crc) {
		throw std::runtime_error(std::string(CORRUPT_HEADER));
	}
	// A zero-length payload is never produced (embedRedditPngPayload refuses an
	// empty one), so reject it here rather than leaving it to be caught further
	// downstream by the profile check: crc32 of nothing is a value an attacker
	// can put in the header, and this is the cheapest place to say no.
	const std::uint32_t declared_size = readLe32(header.data() + 12);
	if (declared_size == 0 || declared_size > payload_capacity) {
		throw std::runtime_error(std::string(CORRUPT_HEADER));
	}
}

[[nodiscard]] vBytes extractPayload(
	const RedditPngCarrier& carrier,
	const KeyedPermutation& permutation,
	const CarrierMasks& masks,
	std::size_t payload_size) {

	vBytes payload(payload_size, 0);
	const std::size_t nibble_count = checkedMulSize(
		payload_size,
		2,
		"File Recovery Error: Reddit payload size overflow.");
	std::vector<std::size_t> positions(
		std::min(nibble_count, GROUPS_PER_BATCH) * GROUP_SAMPLES);
	for (std::size_t batch_start = 0; batch_start < nibble_count; batch_start += GROUPS_PER_BATCH) {
		const std::size_t batch_groups = std::min(GROUPS_PER_BATCH, nibble_count - batch_start);
		const auto batch = std::span(positions).first(batch_groups * GROUP_SAMPLES);
		permutation.map(HEADER_SLOTS + batch_start * GROUP_SAMPLES, batch);
		// Every read below is a cache miss on a large cover; issuing them all
		// first lets them overlap.
		for (const std::size_t sample : batch) {
			__builtin_prefetch(carrier.rgb.data() + sample, 0, 0);
		}

		for (std::size_t in_batch = 0; in_batch < batch_groups; ++in_batch) {
			const std::size_t group = batch_start + in_batch;
			unsigned syndrome = 0;
			for (std::size_t position = 0; position < GROUP_SAMPLES; ++position) {
				if ((carrier.rgb[batch[in_batch * GROUP_SAMPLES + position]] & 1U) != 0U) {
					syndrome ^= static_cast<unsigned>(position + 1U);
				}
			}

			const unsigned nibble = syndrome ^ masks.payload(group);
			if ((group & 1U) == 0U) {
				payload[group / 2U] = static_cast<Byte>(nibble << 4U);
			} else {
				payload[group / 2U] |= static_cast<Byte>(nibble);
			}
		}
	}
	return payload;
}

[[nodiscard]] bool isPdvrdtRedditProfile(std::span<const Byte> profile) {
	if (!hasPdvrdtProfileMarkers(profile, DEFAULT_OFFSETS)) {
		return false;
	}
	if (!spanHasRange(
			profile,
			DEFAULT_OFFSETS.encrypted_file,
			minimumStreamCipherSize())) {
		throw std::runtime_error(
			"File Recovery Error: Reddit embedded pdvrdt profile is truncated.");
	}
	return true;
}

[[nodiscard]] vBytes encodeRgbPng(const RedditPngCarrier& carrier) {
	lodepng::State state;
	lodepng_zlib_adapter::configureEncoder(state);
	state.info_raw.colortype = LCT_RGB;
	state.info_raw.bitdepth = 8;
	state.info_png.color.colortype = LCT_RGB;
	state.info_png.color.bitdepth = 8;
	state.info_png.interlace_method = 0;
	state.encoder.auto_convert = 0;

	vBytes encoded;
	const unsigned error = lodepng::encode(
		encoded,
		carrier.rgb.data(),
		carrier.width,
		carrier.height,
		state);
	if (error != 0) {
		throw std::runtime_error(std::format(
			"LodePNG Reddit encode error {}: {}",
			error,
			lodepng_error_text(error)));
	}
	return encoded;
}

struct DecodedCarrier {
	RedditPngCarrier carrier{};
	bool fully_opaque{true};
};

[[nodiscard]] unsigned tryDecodeRgba(
	const vBytes& png,
	vBytes& rgba,
	unsigned& width,
	unsigned& height) {

	lodepng::State state;
	lodepng_zlib_adapter::configureDecoder(state);
	state.info_raw.colortype = LCT_RGBA;
	state.info_raw.bitdepth = 8;
	return lodepng::decode(rgba, width, height, state, png);
}

[[nodiscard]] DecodedCarrier decodeCarrier(vBytes& png) {
	// Measured note: decoding straight into the carrier buffer and compacting
	// RGBA -> RGB in place was tried here, on the theory that it would cut this
	// function's footprint from 7 bytes per pixel to 4. It does not. lodepng
	// holds its own inflate buffer (the filtered scanlines) alongside the raw
	// RGBA output, and that pair already sets the high-water mark; the separate
	// RGB buffer below is allocated only after the inflate buffer is freed and
	// simply reuses that space. Peak RSS on a 49-megapixel cover was 379 MiB
	// either way. Not worth the in-place aliasing, so it stays a plain copy.
	vBytes rgba;
	unsigned width = 0;
	unsigned height = 0;
	unsigned error = tryDecodeRgba(png, rgba, width, height);

	// Same repair the conceal path performs: a spare IDAT appended by another
	// tool leaves bytes past the end of the PNG's own zlib stream, which the
	// decoder refuses. Those bytes are not pixels, so drop them and try again
	// rather than failing on an image whose carrier is perfectly intact.
	// Attempted on the failure, not as a pre-pass -- locating the end of the
	// stream costs a full inflate, and a clean image should not pay for it.
	//
	// The repair works in place, so an untrusted multi-gigabyte file costs no
	// second copy of itself on the way to being rejected.
	if (error == lodepng_zlib_adapter::ERROR_TRAILING_DATA && stripTrailingIdatData(png)) {
		rgba.clear();
		width = 0;
		height = 0;
		error = tryDecodeRgba(png, rgba, width, height);
	}

	if (error == lodepng_zlib_adapter::ERROR_TRAILING_DATA) {
		// lodepng passes custom_zlib codes straight through, so without this the
		// failure surfaces as "unknown error code 1052".
		throw std::runtime_error(
			"PNG Error: The image's compressed image data does not end where the PNG says it "
			"should, and the leftover bytes are not a stray chunk that can be removed.");
	}
	if (error != 0) {
		throw std::runtime_error(std::format(
			"LodePNG Reddit decode error {}: {}",
			error,
			lodepng_error_text(error)));
	}

	const std::size_t pixels = checkedMulSize(
		static_cast<std::size_t>(width),
		static_cast<std::size_t>(height),
		"Image Size Error: Reddit carrier pixel count overflow.");
	const std::size_t expected_rgba = checkedMulSize(
		pixels, 4, "Image Size Error: Reddit RGBA buffer size overflow.");
	if (rgba.size() != expected_rgba) {
		throw std::runtime_error(
			"Image Decode Error: Reddit carrier has an unexpected RGBA buffer size.");
	}

	DecodedCarrier decoded{
		.carrier = RedditPngCarrier{
			.width = width,
			.height = height
		}
	};
	decoded.carrier.rgb.resize(checkedMulSize(
		pixels, 3, "Image Size Error: Reddit RGB buffer size overflow."));
	for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
		const std::size_t rgba_offset = pixel * 4U;
		const std::size_t rgb_offset = pixel * 3U;
		if (rgba[rgba_offset + 3U] != 255U) {
			decoded.fully_opaque = false;
		}
		std::memcpy(
			decoded.carrier.rgb.data() + static_cast<std::ptrdiff_t>(rgb_offset),
			rgba.data() + static_cast<std::ptrdiff_t>(rgba_offset),
			3);
	}

	decoded.carrier.payload_capacity =
		theoreticalCapacity(decoded.carrier.rgb.size());
	return decoded;
}

// Only a missing magic permits another key/version attempt. Once the header
// is identified, bad versions, lengths and CRCs propagate as errors. Return raw
// payload here so an unrelated PNGSTEG1 payload also ends the version search;
// the pdvrdt profile check follows selection of the matching carrier.
[[nodiscard]] std::optional<vBytes> tryExtractPayload(
	const RedditPngCarrier& carrier,
	const KeyedPermutation& permutation,
	CarrierMasks& masks,
	Byte expected_version) {

	const std::array<Byte, HEADER_SIZE> header = extractHeader(carrier, permutation, masks);
	if (!std::equal(CARRIER_MAGIC.begin(), CARRIER_MAGIC.end(), header.begin())) {
		return std::nullopt;
	}

	validateCarrierHeader(header, carrier.payload_capacity, expected_version);
	const std::size_t payload_size = readLe32(header.data() + 12);
	masks.preparePayload(payload_size);
	vBytes payload = extractPayload(carrier, permutation, masks, payload_size);
	if (pdvrdtCrc32Update(0, payload) != readLe32(header.data() + 16)) {
		throw std::runtime_error("File Recovery Error: Reddit embedded data is corrupt.");
	}
	return payload;
}

// Versions 1 and 2 share the splitmix64 construction under a 64-bit key.
[[nodiscard]] std::optional<vBytes> tryExtractPayloadWithKey(
	const RedditPngCarrier& carrier,
	std::uint64_t carrier_key,
	Byte expected_version) {

	const KeyedPermutation permutation(carrier.rgb.size(), carrier_key);
	CarrierMasks masks(carrier_key);
	return tryExtractPayload(carrier, permutation, masks, expected_version);
}

[[nodiscard]] std::optional<vBytes> tryExtractPayloadWithKey(
	const RedditPngCarrier& carrier,
	std::span<const Byte, CarrierKeys::V3_KEY_BYTES> carrier_key,
	Byte expected_version) {

	const KeyedPermutation permutation(carrier.rgb.size(), carrier_key);
	CarrierMasks masks(carrier_key);
	return tryExtractPayload(carrier, permutation, masks, expected_version);
}

} // namespace

RedditPngCarrier prepareRedditPngCarrier(vBytes& png) {
	DecodedCarrier decoded = decodeCarrier(png);
	if (!decoded.fully_opaque) {
		throw std::runtime_error(
			"Image Format Error: Reddit cover contains transparency; flatten it first.");
	}
	return std::move(decoded.carrier);
}

vBytes embedRedditPngPayload(
	RedditPngCarrier& carrier,
	const CarrierKeys& carrier_keys,
	std::span<const Byte> payload) {

	if (payload.empty()) {
		throw std::runtime_error("Data File Size Error: Reddit carrier payload is empty.");
	}
	if (payload.size() > carrier.payload_capacity) {
		throw std::runtime_error(
			"Data File Size Error: Encrypted Reddit payload exceeds the cover image's theoretical adaptive capacity.");
	}

	const std::array<Byte, HEADER_SIZE> header = makeHeader(payload);
	const KeyedPermutation permutation(carrier.rgb.size(), carrier_keys.v3);
	CarrierMasks masks(carrier_keys.v3);
	masks.preparePayload(payload.size());
	const TieBreakBits ties(carrier_keys.v3, HEADER_SLOTS + payload.size() * 2U * GROUP_SAMPLES);
	const vBytes activity = buildActivityMap(carrier);
	embedHeader(carrier, activity, permutation, masks, ties, header);
	embedPayload(carrier, activity, permutation, masks, ties, payload);
	return encodeRgbPng(carrier);
}

std::optional<RedditPngCarrier> decodeRedditRecoveryCarrier(vBytes& png) {
	// Recovery is the one path that hands fully untrusted bytes to the PNG
	// decoder -- conceal has already run this check inside its cover preflight,
	// but nothing on the way here has. Without it a few hundred KB of crafted PNG
	// can declare a geometry that drives a multi-gigabyte decode.
	// Only the geometry bound is needed here -- and only it is available to the
	// recover-only web binary, which does not link image.cpp.
	requirePngGeometryWithinDecodeLimit(png);

	DecodedCarrier decoded = decodeCarrier(png);
	if (!decoded.fully_opaque || decoded.carrier.rgb.size() <= HEADER_SLOTS) {
		return std::nullopt;
	}
	return std::move(decoded.carrier);
}

std::optional<vBytes> extractRedditPngPayload(
	const RedditPngCarrier& carrier,
	const SensitiveU64& recovery_pin) {

	// Derivation failures must propagate: falling back after an Argon2
	// allocation error would conceal an operational failure. One Argon2 run
	// yields the version-3 and version-2 keys; only version 1 needs another,
	// cheap, derivation.
	CarrierKeys carrier_keys;
	deriveCarrierKeysFromPin(recovery_pin, carrier_keys);
	auto payload = tryExtractPayloadWithKey(carrier, carrier_keys.v3, CARRIER_VERSION);
	if (!payload) {
		payload = tryExtractPayloadWithKey(carrier, carrier_keys.v2.value, V2_CARRIER_VERSION);
	}
	if (!payload) {
		SensitiveU64 legacy_key;
		deriveLegacyCarrierKeyFromPin(recovery_pin, legacy_key);
		payload = tryExtractPayloadWithKey(carrier, legacy_key.value, LEGACY_CARRIER_VERSION);
	}
	if (!payload || !isPdvrdtRedditProfile(*payload)) {
		return std::nullopt;
	}
	return payload;
}
