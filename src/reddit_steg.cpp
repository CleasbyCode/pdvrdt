#include "reddit_steg.h"

#include "encryption.h"
#include "io_utils.h"
#include "lodepng/lodepng_config.h"
#include "lodepng/lodepng.h"
#include "lodepng/lodepng_zlib_adapter.h"
#include "parallel.h"
#include "png_utils.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <zlib.h>

#include <atomic>

namespace {

// Carrier keying.
//
// Every sample position and whitening bit derives from the carrier key, so
// finding the known header under a candidate key confirms that an image carries
// a payload. Version 4 keys ChaCha20 with a 256-bit key from Argon2id (see
// deriveCarrierKeyFromRecoveryKey): the Feistel round functions are tables of ChaCha20
// output and the whitening bits are ChaCha20 keystream, so there is no cheaper
// way to test a key than the Argon2 derivation itself. Carriers made before
// pdvrdt 6.0 (versions 1-3, keyed by a numeric PIN) are not read.
//
// Payload confidentiality is provided by secretstream with its separate,
// randomly salted Argon2id key.
constexpr auto CARRIER_MAGIC = std::to_array<Byte>({
	'P', 'N', 'G', 'S', 'T', 'E', 'G', '1'
});

constexpr Byte
	CARRIER_VERSION = 4,
	// Scheme 2: KeyedPermutation uses the next power-of-two domain rather than
	// the next even-power one.
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

// Separate ChaCha20 (IETF, RFC 8439) streams under the carrier key, told apart
// by their 96-bit nonce, each starting at block 0. The nonce still reads
// "PDVRDTv3": version 4 changed only the key derivation, and the nonce is part
// of the wire format.
enum class KeyStream : Byte { permutation = 1, header = 2, payload = 3, tie_break = 4 };

void chachaKeystream(
	std::span<const Byte, CarrierKey::BYTES> key,
	KeyStream stream,
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
class KeyedPermutation {
public:
	static constexpr std::size_t ROUNDS = 8;

	// Each round function is a table with one entry per possible
	// input half, filled from ChaCha20 -- a keyed random function rather than a
	// fast mixer of a small key. Round r's table occupies entries
	// [r * 2^right_bits_, (r + 1) * 2^right_bits_) of one keystream, read as
	// little-endian 32-bit words masked to the round's output width; odd rounds,
	// whose input is only left_bits_ wide, use the first 2^left_bits_ of theirs.
	// At the largest cover accepted that is 8 * 2^14 words: 512 KiB, filled by
	// one bulk keystream call.
	KeyedPermutation(std::uint64_t size, std::span<const Byte, CarrierKey::BYTES> key)
		: size_(size) {
		initGeometry();
		table_stride_ = std::size_t{1} << right_bits_;
		tables_.resize(ROUNDS * table_stride_);
		vBytes keystream(tables_.size() * 4U);
		ScopedWipe keystream_wipe{keystream};
		chachaKeystream(key, KeyStream::permutation, keystream);
		for (std::size_t round = 0; round < ROUNDS; ++round) {
			const auto out_mask = static_cast<std::uint32_t>(roundMask(round));
			for (std::size_t i = 0; i < table_stride_; ++i) {
				const std::size_t entry = round * table_stride_ + i;
				tables_[entry] = readLe32(keystream.data() + entry * 4U) & out_mask;
			}
		}
	}

	// The tables are as good as the carrier key: they locate the payload.
	~KeyedPermutation() {
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
	// outside the domain go round again. Every value is exactly what a single
	// lookup returns.
	void map(std::uint64_t first, std::span<std::size_t> out) const {
		if (first > size_ || out.size() > size_ - first) {
			throw std::runtime_error("Internal Error: Reddit carrier index is out of range.");
		}
		mapBlocks(first, out);
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

	[[nodiscard]] std::uint64_t permutePowerOfTwo(std::uint64_t value) const noexcept {
		std::uint64_t left = value >> right_bits_;
		std::uint64_t right = value & right_mask_;
		for (std::size_t round = 0; round < ROUNDS; ++round) {
			const std::uint64_t function = tables_[round * table_stride_ + right];
			const std::uint64_t next_right = (left ^ function) & roundMask(round);
			left = right;
			right = next_right;
		}
		return (left << right_bits_) | right;
	}

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
					values[i] = permutePowerOfTwo(values[i]);
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

	std::vector<std::uint32_t> tables_{};
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

// Header and payload whitening: the header is XORed with 24 keystream bytes
// (bit i is bit 7 - i % 8 of byte i / 8) and each payload nibble with the
// matching nibble of a keystream as long as the payload.
class CarrierMasks {
public:
	explicit CarrierMasks(std::span<const Byte, CarrierKey::BYTES> key) {
		std::copy(key.begin(), key.end(), key_.begin());
		chachaKeystream(key_, KeyStream::header, header_);
	}

	~CarrierMasks() {
		sodium_memzero(key_.data(), key_.size());
		sodium_memzero(header_.data(), header_.size());
		if (!payload_.empty()) {
			sodium_memzero(payload_.data(), payload_.size());
		}
	}
	CarrierMasks(const CarrierMasks&) = delete;
	CarrierMasks& operator=(const CarrierMasks&) = delete;

	// The payload keystream is sized once the payload length is known.
	void preparePayload(std::size_t payload_size) {
		payload_.resize(payload_size);
		chachaKeystream(key_, KeyStream::payload, payload_);
	}

	[[nodiscard]] unsigned header(std::size_t bit_index) const noexcept {
		return (header_[bit_index / 8U] >> (7U - (bit_index % 8U))) & 1U;
	}

	[[nodiscard]] unsigned payload(std::size_t group_index) const noexcept {
		const Byte mask = payload_[group_index / 2U];
		return (group_index & 1U) == 0U ? mask >> 4U : mask & 0x0fU;
	}

private:
	std::array<Byte, CarrierKey::BYTES> key_{};
	std::array<Byte, HEADER_SIZE> header_{};
	vBytes payload_{};
};

// Conceal's tie-break between equally costly +1 and -1 changes. Recovery never
// reads it, so it only needs to be unbiased; it is indexed by carrier ordinal,
// the order samples are visited, so it is read sequentially rather than missing
// cache on every tie as a per-sample table did.
class TieBreakBits {
public:
	TieBreakBits(std::span<const Byte, CarrierKey::BYTES> key, std::size_t ordinals)
		: bits_((ordinals + 7U) / 8U) {
		chachaKeystream(key, KeyStream::tie_break, bits_);
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

// Per-pixel 3x3 sums of the activity map (edges replicated): the input to every
// sample's base cost. At most 9 * 255, so 16 bits hold it.
using ActivitySums = std::vector<std::uint16_t>;
constexpr std::size_t MAX_ACTIVITY_SUM = 9U * 255U;

// Activity is the smallest of four directional luminance differences around a
// pixel (edges replicated); its 3x3 sum is what the cost needs, so only the
// sums are kept. Luminance is computed once per pixel rather than nine times.
// The loops index raw pointers: every offset is clamped into the image, and
// the bounds-checked containers would otherwise check each of ~20 reads per
// pixel.
[[nodiscard]] ActivitySums buildActivitySums(const RedditPngCarrier& carrier, unsigned threads = workerCount()) {
	if (carrier.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
		carrier.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
		throw std::runtime_error("Image Size Error: Reddit carrier dimensions are too large.");
	}
	const std::size_t width = carrier.width;
	const std::size_t height = carrier.height;
	const std::size_t pixels = checkedMulSize(
		width, height, "Image Size Error: Reddit carrier pixel count overflow.");
	if (pixels == 0 || carrier.rgb.size() != pixels * 3U) {
		throw std::runtime_error("Internal Error: Reddit carrier geometry mismatch.");
	}
	// Each pass is split by rows; a row reads only the finished plane before it.
	constexpr std::size_t MIN_ROWS = 64;

	vBytes luma(pixels);
	parallelFor(height, MIN_ROWS, threads, [&](std::size_t first_row, std::size_t end_row) {
		const Byte* rgb = carrier.rgb.data() + first_row * width * 3U;
		Byte* out = luma.data() + first_row * width;
		for (std::size_t pixel = 0; pixel < (end_row - first_row) * width; ++pixel, rgb += 3) {
			out[pixel] = static_cast<Byte>((77U * rgb[0] + 150U * rgb[1] + 29U * rgb[2] + 128U) >> 8U);
		}
	});

	vBytes activity(pixels);
	parallelFor(height, MIN_ROWS, threads, [&](std::size_t first_row, std::size_t end_row) {
		const auto score = [](int center, int first, int second) static {
			return (std::abs(center - first) + std::abs(center - second) + std::abs(first - second)) / 3;
		};
		for (std::size_t y = first_row; y < end_row; ++y) {
			const Byte* up = luma.data() + (y > 0 ? y - 1 : 0) * width;
			const Byte* mid = luma.data() + y * width;
			const Byte* down = luma.data() + (y + 1 < height ? y + 1 : y) * width;
			Byte* out = activity.data() + y * width;
			for (std::size_t x = 0; x < width; ++x) {
				const std::size_t left = x > 0 ? x - 1 : 0;
				const std::size_t right = x + 1 < width ? x + 1 : x;
				const int center = mid[x];
				const int horizontal = score(center, mid[left], mid[right]);
				const int vertical = score(center, up[x], down[x]);
				const int diagonal_down = score(center, up[left], down[right]);
				const int diagonal_up = score(center, up[right], down[left]);
				out[x] = static_cast<Byte>(std::min({horizontal, vertical, diagonal_down, diagonal_up, 255}));
			}
		}
	});
	vBytes{}.swap(luma);

	ActivitySums sums(pixels);
	parallelFor(height, MIN_ROWS, threads, [&](std::size_t first_row, std::size_t end_row) {
		for (std::size_t y = first_row; y < end_row; ++y) {
			const Byte* rows[3] = {
				activity.data() + (y > 0 ? y - 1 : 0) * width,
				activity.data() + y * width,
				activity.data() + (y + 1 < height ? y + 1 : y) * width,
			};
			std::uint16_t* out = sums.data() + y * width;
			for (std::size_t x = 0; x < width; ++x) {
				const std::size_t left = x > 0 ? x - 1 : 0;
				const std::size_t right = x + 1 < width ? x + 1 : x;
				unsigned total = 0;
				for (const Byte* row : rows) {
					total += static_cast<unsigned>(row[left]) + row[x] + row[right];
				}
				out[x] = static_cast<std::uint16_t>(total);
			}
		}
	});
	return sums;
}

// 1 + 256 / (mean activity + 4) for every possible 3x3 sum, evaluated exactly
// as the per-sample formula always has been, so a lookup gives the same double.
[[nodiscard]] const std::array<double, MAX_ACTIVITY_SUM + 1>& baseCostTable() {
	static const auto table = [] {
		std::array<double, MAX_ACTIVITY_SUM + 1> values{};
		for (std::size_t sum = 0; sum <= MAX_ACTIVITY_SUM; ++sum) {
			const double local_activity = static_cast<double>(sum) / 9.0;
			values[sum] = 1.0 + 256.0 / (local_activity + 4.0);
		}
		return values;
	}();
	return table;
}

// The cost of changing one carrier sample by -1 or +1: a base cost from local
// image activity and channel sensitivity, plus how far the new value would sit
// from the same channel of its eight neighbours (edges replicated). Reads the
// carrier as it is being changed, so earlier changes count.
class SampleCoster {
public:
	SampleCoster(const RedditPngCarrier& carrier, const ActivitySums& sums)
		: rgb_(carrier.rgb.data()),
		  sums_(sums.data()),
		  base_(baseCostTable().data()),
		  width_(carrier.width),
		  height_(carrier.height),
		  row_bytes_(static_cast<std::size_t>(carrier.width) * 3U),
		  rgb_size_(carrier.rgb.size()) {
		if (sums.size() * 3U != carrier.rgb.size()) {
			throw std::runtime_error("Internal Error: Reddit carrier cost map mismatch.");
		}
	}

	[[nodiscard]] ChangeChoice choose(std::size_t sample, bool tie_takes_higher) const noexcept {
		constexpr std::array<double, 3> CHANNEL_WEIGHT = {1.05, 1.25, 0.90};
		const std::size_t pixel = sample / 3U;
		const std::size_t channel = sample - pixel * 3U;
		const std::size_t y = pixel / width_;
		const std::size_t x = pixel - y * width_;
		const double base_cost = base_[sums_[pixel]] * CHANNEL_WEIGHT[channel];

		const std::size_t left = (x > 0 ? x - 1 : 0) * 3U;
		const std::size_t centre = x * 3U;
		const std::size_t right = (x + 1 < width_ ? x + 1 : x) * 3U;
		const Byte* up = rgb_ + (y > 0 ? y - 1 : 0) * row_bytes_ + channel;
		const Byte* mid = rgb_ + y * row_bytes_ + channel;
		const Byte* down = rgb_ + (y + 1 < height_ ? y + 1 : y) * row_bytes_ + channel;
		const int neighbours[8] = {
			up[left], up[centre], up[right],
			mid[left], mid[right],
			down[left], down[centre], down[right],
		};

		const int original = mid[centre];
		unsigned lower_total = 0;
		unsigned higher_total = 0;
		for (const int neighbour : neighbours) {
			lower_total += static_cast<unsigned>(std::abs(original - 1 - neighbour));
			higher_total += static_cast<unsigned>(std::abs(original + 1 - neighbour));
		}

		// -1 is considered first and +1 replaces it only when cheaper, or when
		// equally cheap and the tie-break says so.
		ChangeChoice best{
			.cost = std::numeric_limits<double>::infinity(),
			.value = static_cast<Byte>(original)
		};
		if (original > 0) {
			best = {.cost = base_cost + static_cast<double>(lower_total) / 512.0,
				.value = static_cast<Byte>(original - 1)};
		}
		if (original < 255) {
			const double cost = base_cost + static_cast<double>(higher_total) / 512.0;
			if (cost < best.cost || (cost == best.cost && tie_takes_higher)) {
				best = {.cost = cost, .value = static_cast<Byte>(original + 1)};
			}
		}
		return best;
	}

	// Issues the cache misses a group's costing will take, so they overlap.
	void prefetch(std::span<const std::size_t> samples) const noexcept {
		for (const std::size_t sample : samples) {
			const Byte* const base = rgb_ + sample;
			__builtin_prefetch(base, 0, 1);
			if (sample >= row_bytes_) {
				__builtin_prefetch(base - row_bytes_, 0, 1);
			}
			if (sample + row_bytes_ < rgb_size_) {
				__builtin_prefetch(base + row_bytes_, 0, 1);
			}
			__builtin_prefetch(sums_ + sample / 3U, 0, 1);
		}
	}

private:
	const Byte* rgb_;
	const std::uint16_t* sums_;
	const double* base_;
	std::size_t width_;
	std::size_t height_;
	std::size_t row_bytes_;
	std::size_t rgb_size_;
};

void embedHeader(
	RedditPngCarrier& carrier,
	const SampleCoster& coster,
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
				carrier.rgb[sample] = coster.choose(sample, ties.takeHigher(ordinal)).value;
			}
		}
	}
}

// Groups whose sample positions are resolved per batched permutation call.
constexpr std::size_t GROUPS_PER_BATCH = 256;

// The samples to change so that a group's syndrome moves by `delta`: the one
// sample `delta` names, or two whose positions XOR to `delta` if together they
// cost less (the cheapest such pair; the earlier pair on a tie). GROUP_SAMPLES
// marks "none": both are none when delta is zero.
struct ChangePair {
	std::size_t first{GROUP_SAMPLES};
	std::size_t second{GROUP_SAMPLES};
};

[[nodiscard]] ChangePair cheapestChange(const ChangeChoice* choices, unsigned delta) noexcept {
	if (delta == 0U) {
		return {};
	}
	const std::size_t single = static_cast<std::size_t>(delta - 1U);
	double best_cost = choices[single].cost;
	ChangePair pair{.first = single};
	for (unsigned first_index = 1; first_index <= 15; ++first_index) {
		const unsigned second_index = first_index ^ delta;
		if (second_index < 1U || second_index > 15U || first_index >= second_index) {
			continue;
		}
		const double pair_cost = choices[first_index - 1U].cost + choices[second_index - 1U].cost;
		if (pair_cost < best_cost) {
			best_cost = pair_cost;
			pair = {.first = first_index - 1U, .second = second_index - 1U};
		}
	}
	return pair;
}

// The one-thread form of embedPayload(): each group is costed and committed in
// turn, with no speculation to check. Same decisions, same output.
void embedPayloadSerial(
	RedditPngCarrier& carrier,
	const SampleCoster& coster,
	const KeyedPermutation& permutation,
	const CarrierMasks& masks,
	const TieBreakBits& ties,
	std::span<const Byte> payload) {

	const std::size_t nibble_count = payload.size() * 2U;
	std::vector<std::size_t> positions(std::min(nibble_count, GROUPS_PER_BATCH) * GROUP_SAMPLES);
	Byte* const rgb = carrier.rgb.data();
	for (std::size_t batch_start = 0; batch_start < nibble_count; batch_start += GROUPS_PER_BATCH) {
		const std::size_t batch_groups = std::min(GROUPS_PER_BATCH, nibble_count - batch_start);
		const std::span<std::size_t> batch = std::span(positions).first(batch_groups * GROUP_SAMPLES);
		permutation.map(HEADER_SLOTS + batch_start * GROUP_SAMPLES, batch);
		coster.prefetch(batch.first(GROUP_SAMPLES));

		for (std::size_t in_batch = 0; in_batch < batch_groups; ++in_batch) {
			const std::size_t group = batch_start + in_batch;
			const std::size_t first_ordinal = HEADER_SLOTS + group * GROUP_SAMPLES;
			// Raw pointers: the permutation only yields indices inside the carrier.
			const std::size_t* const samples = batch.data() + in_batch * GROUP_SAMPLES;
			if (in_batch + 1U < batch_groups) {
				coster.prefetch(batch.subspan((in_batch + 1U) * GROUP_SAMPLES, GROUP_SAMPLES));
			}
			ChangeChoice choices[GROUP_SAMPLES];
			unsigned syndrome = 0;
			for (std::size_t position = 0; position < GROUP_SAMPLES; ++position) {
				syndrome ^= (rgb[samples[position]] & 1U) * static_cast<unsigned>(position + 1U);
				choices[position] = coster.choose(samples[position], ties.takeHigher(first_ordinal + position));
			}
			const unsigned nibble = (group & 1U) == 0U
				? payload[group / 2U] >> 4U
				: payload[group / 2U] & 0x0fU;
			const ChangePair pair = cheapestChange(choices, syndrome ^ nibble ^ masks.payload(group));
			if (pair.first != GROUP_SAMPLES) {
				rgb[samples[pair.first]] = choices[pair.first].value;
			}
			if (pair.second != GROUP_SAMPLES) {
				rgb[samples[pair.second]] = choices[pair.second].value;
			}
		}
	}
}

// Marks of carrier samples whose costing may have changed since a speculative
// pass read them. Changing sample s can alter the costing of s itself and of
// the same channel in the eight pixels around it -- the only carrier values a
// costing reads -- so a change marks that 3x3 block for its channel. One bit
// per sample.
class StaleSamples {
public:
	StaleSamples(std::size_t samples, std::size_t width, std::size_t height)
		: bits_((samples + 63U) / 64U), width_(width), height_(height) {}

	[[nodiscard]] bool test(std::size_t sample) const noexcept {
		return ((bits_[sample / 64U] >> (sample % 64U)) & 1U) != 0U;
	}

	void markAround(std::size_t sample) noexcept { setAround(sample, true); }
	void clearAround(std::size_t sample) noexcept { setAround(sample, false); }

private:
	void setAround(std::size_t sample, bool on) noexcept {
		const std::size_t pixel = sample / 3U;
		const std::size_t channel = sample - pixel * 3U;
		const std::size_t y = pixel / width_;
		const std::size_t x = pixel - y * width_;
		const std::size_t y_end = std::min(y + 1U, height_ - 1U);
		const std::size_t x_end = std::min(x + 1U, width_ - 1U);
		for (std::size_t ny = y > 0 ? y - 1U : 0; ny <= y_end; ++ny) {
			for (std::size_t nx = x > 0 ? x - 1U : 0; nx <= x_end; ++nx) {
				const std::size_t target = (ny * width_ + nx) * 3U + channel;
				const std::uint64_t mask = std::uint64_t{1} << (target % 64U);
				if (on) {
					bits_[target / 64U] |= mask;
				} else {
					bits_[target / 64U] &= ~mask;
				}
			}
		}
	}

	std::vector<std::uint64_t> bits_;
	std::size_t width_;
	std::size_t height_;
};

// Embed the payload nibbles, a group of 15 samples per nibble, changing at
// most two samples a group.
//
// The work is memory-bound: the permutation deliberately scatters a group's
// samples across the whole image, so each costing stalls on cold cache lines.
// The groups are therefore taken a chunk at a time, and for every group in the
// chunk the sample positions, the 15 costings and the current syndrome are
// worked out in parallel against the carrier as it stands. The decisions are
// then committed strictly in group order on this thread. A group whose samples
// were marked stale by a change committed earlier in the same chunk has those
// costings (and its syndrome) redone first, so every decision is made on
// exactly the carrier the one-group-at-a-time algorithm would see, and the
// output does not depend on the thread count. Marks are rare -- a chunk's
// changes touch a small fraction of a large carrier -- so most groups commit
// as costed.
//
// Within a range, positions are resolved a batch at a time and the next
// group's neighbourhoods are prefetched while this one is costed, so the
// misses overlap.
//
// Tried and measured, not worth re-attempting: costing each chunk's samples in
// memory order (fully sorted, or bucketed by image row) so neighbouring
// samples share cache lines. The cost loop itself ran 28-35% faster, but
// building the order took as long again: on an 11 MP cover the speculation
// step went 524 -> 489 ms at best on 16 threads (worse on 6 MP), and on one
// thread 2.83 -> 3.02 s. A costing reads only nine bytes, so there is too
// little to reuse to pay for the reordering.
void embedPayload(
	RedditPngCarrier& carrier,
	const SampleCoster& coster,
	const KeyedPermutation& permutation,
	const CarrierMasks& masks,
	const TieBreakBits& ties,
	std::span<const Byte> payload,
	unsigned threads = workerCount()) {

	const std::size_t nibble_count = checkedMulSize(
		payload.size(), 2, "Data File Size Error: Reddit payload size overflow.");
	if (nibble_count == 0) {
		return;
	}
	if (threads <= 1) {
		embedPayloadSerial(carrier, coster, permutation, masks, ties, payload);
		return;
	}

	// A chunk is sized against the carrier so that the share of stale samples
	// stays near 1%: each committed change marks up to nine.
	constexpr std::size_t MAX_CHUNK_GROUPS = 32768;
	const std::size_t chunk_groups = std::min(nibble_count,
		std::clamp(carrier.rgb.size() / 1024U, GROUPS_PER_BATCH, MAX_CHUNK_GROUPS));

	std::vector<std::size_t> positions(chunk_groups * GROUP_SAMPLES);
	std::vector<ChangeChoice> choices(chunk_groups * GROUP_SAMPLES);
	std::vector<Byte> syndromes(chunk_groups);
	StaleSamples stale(carrier.rgb.size(), carrier.width, carrier.height);
	std::vector<std::size_t> changed;
	changed.reserve(chunk_groups * 2U);
	Byte* const rgb = carrier.rgb.data();

	for (std::size_t chunk_start = 0; chunk_start < nibble_count; chunk_start += chunk_groups) {
		const std::size_t groups = std::min(chunk_groups, nibble_count - chunk_start);

		// Speculate: positions, costings and syndromes, reading the carrier only.
		parallelFor(groups, GROUPS_PER_BATCH, threads, [&](std::size_t begin, std::size_t end) {
			for (std::size_t batch_start = begin; batch_start < end; batch_start += GROUPS_PER_BATCH) {
				const std::size_t batch_groups = std::min(GROUPS_PER_BATCH, end - batch_start);
				const std::span<std::size_t> batch =
					std::span(positions).subspan(batch_start * GROUP_SAMPLES, batch_groups * GROUP_SAMPLES);
				permutation.map(HEADER_SLOTS + (chunk_start + batch_start) * GROUP_SAMPLES, batch);
				coster.prefetch(batch.first(GROUP_SAMPLES));
				for (std::size_t in_batch = 0; in_batch < batch_groups; ++in_batch) {
					const std::size_t index = batch_start + in_batch;
					const std::size_t first_ordinal = HEADER_SLOTS + (chunk_start + index) * GROUP_SAMPLES;
					// Raw pointers: the permutation only yields indices inside the carrier.
					const std::size_t* const samples = positions.data() + index * GROUP_SAMPLES;
					ChangeChoice* const group_choices = choices.data() + index * GROUP_SAMPLES;
					if (in_batch + 1U < batch_groups) {
						coster.prefetch(batch.subspan((in_batch + 1U) * GROUP_SAMPLES, GROUP_SAMPLES));
					}
					unsigned syndrome = 0;
					for (std::size_t position = 0; position < GROUP_SAMPLES; ++position) {
						syndrome ^= (rgb[samples[position]] & 1U) * static_cast<unsigned>(position + 1U);
						group_choices[position] =
							coster.choose(samples[position], ties.takeHigher(first_ordinal + position));
					}
					syndromes[index] = static_cast<Byte>(syndrome);
				}
			}
		});

		// Commit, in group order.
		for (std::size_t index = 0; index < groups; ++index) {
			const std::size_t group = chunk_start + index;
			const std::size_t first_ordinal = HEADER_SLOTS + group * GROUP_SAMPLES;
			const std::size_t* const samples = positions.data() + index * GROUP_SAMPLES;
			ChangeChoice* const group_choices = choices.data() + index * GROUP_SAMPLES;

			unsigned syndrome = syndromes[index];
			if (!changed.empty()) {
				bool recosted = false;
				for (std::size_t position = 0; position < GROUP_SAMPLES; ++position) {
					if (stale.test(samples[position])) {
						group_choices[position] =
							coster.choose(samples[position], ties.takeHigher(first_ordinal + position));
						recosted = true;
					}
				}
				if (recosted) {
					syndrome = 0;
					for (std::size_t position = 0; position < GROUP_SAMPLES; ++position) {
						syndrome ^= (rgb[samples[position]] & 1U) * static_cast<unsigned>(position + 1U);
					}
				}
			}

			const unsigned nibble = (group & 1U) == 0U
				? payload[group / 2U] >> 4U
				: payload[group / 2U] & 0x0fU;
			const ChangePair pair = cheapestChange(group_choices, syndrome ^ nibble ^ masks.payload(group));
			for (const std::size_t position : {pair.first, pair.second}) {
				if (position != GROUP_SAMPLES) {
					rgb[samples[position]] = group_choices[position].value;
					stale.markAround(samples[position]);
					changed.push_back(samples[position]);
				}
			}
		}

		for (const std::size_t sample : changed) {
			stale.clearAround(sample);
		}
		changed.clear();
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
	std::size_t payload_capacity) {

	constexpr std::string_view CORRUPT_HEADER =
		"File Recovery Error: Reddit carrier header is corrupt.";
	if (header[8] != CARRIER_VERSION ||
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

// Read the payload back, a nibble per group. Groups only read the carrier, so
// they are split across threads by payload byte (two nibbles each), and no two
// threads write the same byte.
[[nodiscard]] vBytes extractPayload(
	const RedditPngCarrier& carrier,
	const KeyedPermutation& permutation,
	const CarrierMasks& masks,
	std::size_t payload_size,
	unsigned threads) {

	vBytes payload(payload_size, 0);
	static_cast<void>(checkedMulSize(
		payload_size,
		2,
		"File Recovery Error: Reddit payload size overflow."));
	const Byte* const rgb = carrier.rgb.data();
	Byte* const out = payload.data();

	parallelFor(payload_size, GROUPS_PER_BATCH, threads, [&](std::size_t first_byte, std::size_t end_byte) {
		const std::size_t group_begin = first_byte * 2U;
		const std::size_t group_end = end_byte * 2U;
		std::vector<std::size_t> positions(
			std::min(group_end - group_begin, GROUPS_PER_BATCH) * GROUP_SAMPLES);
		for (std::size_t batch_start = group_begin; batch_start < group_end; batch_start += GROUPS_PER_BATCH) {
			const std::size_t batch_groups = std::min(GROUPS_PER_BATCH, group_end - batch_start);
			const auto batch = std::span(positions).first(batch_groups * GROUP_SAMPLES);
			permutation.map(HEADER_SLOTS + batch_start * GROUP_SAMPLES, batch);
			// Every read below is a cache miss on a large cover; issuing them
			// all first lets them overlap.
			const std::size_t* const samples = batch.data();
			for (std::size_t i = 0; i < batch.size(); ++i) {
				__builtin_prefetch(rgb + samples[i], 0, 0);
			}

			// Raw pointers: the permutation only yields indices inside the carrier.
			for (std::size_t in_batch = 0; in_batch < batch_groups; ++in_batch) {
				const std::size_t group = batch_start + in_batch;
				const std::size_t* const group_samples = samples + in_batch * GROUP_SAMPLES;
				unsigned syndrome = 0;
				for (std::size_t position = 0; position < GROUP_SAMPLES; ++position) {
					syndrome ^= (rgb[group_samples[position]] & 1U) * static_cast<unsigned>(position + 1U);
				}

				const unsigned nibble = syndrome ^ masks.payload(group);
				if ((group & 1U) == 0U) {
					out[group / 2U] = static_cast<Byte>(nibble << 4U);
				} else {
					out[group / 2U] |= static_cast<Byte>(nibble);
				}
			}
		}
	});
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

// PNG filtering of one RGB8 row with lodepng's "minimum sum" choice: each of
// the five filters is tried, scored as the sum of its bytes (filter 0) or of
// their magnitudes read as signed (the rest, with lodepng's 255 - s for
// negatives), and the lowest score wins, the earlier filter on a tie. `prev`
// is a zero row for the first scanline, which gives the same bytes as the
// filters' no-previous-line forms. Every loop is branch-free so it vectorises.
void filterRgbRow(const Byte* __restrict row, const Byte* __restrict prev, std::size_t length,
	std::array<Byte*, 5> attempt, Byte* __restrict out) noexcept {
	constexpr std::size_t BPP = 3;
	const auto paeth = [](int a, int b, int c) static {
		const int pa = std::abs(b - c);
		const int pb = std::abs(a - c);
		const int pc = std::abs(a + b - c - c);
		const int near = pb < pa ? b : a;
		const int near_distance = pb < pa ? pb : pa;
		return pc < near_distance ? c : near;
	};
	// Five separate buffers, none of them the input rows: __restrict lets the
	// compiler vectorise without run-time overlap checks.
	Byte* __restrict const sub = attempt[1];
	Byte* __restrict const up = attempt[2];
	Byte* __restrict const average = attempt[3];
	Byte* __restrict const predicted = attempt[4];
	// The first pixel has nothing to its left: a = c = 0.
	for (std::size_t i = 0; i < std::min(BPP, length); ++i) {
		const int x = row[i];
		const int b = prev[i];
		sub[i] = static_cast<Byte>(x);
		up[i] = static_cast<Byte>(x - b);
		average[i] = static_cast<Byte>(x - (b >> 1));
		predicted[i] = static_cast<Byte>(x - paeth(0, b, 0));
	}
	for (std::size_t i = BPP; i < length; ++i) {
		const int x = row[i];
		const int a = row[i - BPP];
		const int b = prev[i];
		const int c = prev[i - BPP];
		sub[i] = static_cast<Byte>(x - a);
		up[i] = static_cast<Byte>(x - b);
		average[i] = static_cast<Byte>(x - ((a + b) >> 1));
		predicted[i] = static_cast<Byte>(x - paeth(a, b, c));
	}

	std::size_t smallest = 0;
	for (std::size_t i = 0; i < length; ++i) {
		smallest += row[i];
	}
	std::size_t best = 0;
	for (std::size_t type = 1; type < 5; ++type) {
		std::size_t sum = 0;
		const Byte* values = attempt[type];
		for (std::size_t i = 0; i < length; ++i) {
			const unsigned value = values[i];
			sum += value < 128U ? value : 255U - value;
		}
		if (sum < smallest) {
			smallest = sum;
			best = type;
		}
	}
	out[0] = static_cast<Byte>(best);
	std::memcpy(out + 1, best == 0 ? row : attempt[best], length);
}

void appendBigEndian32(Byte* out, std::uint32_t value) noexcept {
	out[0] = static_cast<Byte>(value >> 24U);
	out[1] = static_cast<Byte>(value >> 16U);
	out[2] = static_cast<Byte>(value >> 8U);
	out[3] = static_cast<Byte>(value);
}

// The zlib stream as consecutive pieces, so the encoder can copy each into
// the PNG and free it at once, never holding the whole stream twice.
using ZlibPieces = std::vector<vBytes>;

constexpr std::size_t ZLIB_SLICE_BYTES = 1uz << 20;

// Whether the PNG stream is worth compressing in slices. The sliced pass ends
// when its busiest thread does, and per byte zlib level 6 costs that thread
// about 2.3x what one libdeflate pass over the whole stream costs per byte
// (measured on photographs, i9-9900K; ~1.5x on noise). So slicing pays only
// once the stream is more than 2.5 times the busiest thread's share. That
// share is whole slices dealt round the threads, plus the short tail slice
// when every thread already has the same number of full ones. At 16 threads
// the crossover is near 2.5 MiB (a 1000x1000 cover); one full slice and a
// tail (just over 1 MiB) took 26 ms sliced against 17 ms single. Two threads
// never qualify.
[[nodiscard]] bool useSlicedZlib(std::size_t size, unsigned threads) noexcept {
	if (threads < 2 || size <= ZLIB_SLICE_BYTES) return false;
	const std::size_t full_slices = size / ZLIB_SLICE_BYTES;
	const std::size_t tail = size % ZLIB_SLICE_BYTES;
	const std::size_t rounds = (full_slices + threads - 1U) / threads;
	const std::size_t busiest = rounds * ZLIB_SLICE_BYTES + (full_slices % threads == 0 ? tail : 0);
	// size > 2.5 * busiest, in integers.
	return size * 2U > busiest * 5U;
}

// zlib-compress `data` at level 6 in parallel, pigz-style: fixed 1 MiB slices
// are deflated independently -- each primed with the 32 KiB before it as its
// dictionary, so matches still reach back across the seam, and ended with a
// sync flush, so the slices concatenate into one valid stream -- and their
// Adler-32 checksums are combined. A single deflate pass is the encode's whole
// cost on one core; this spreads it, at ~1% larger output. The slice size, not
// the thread count, decides the bytes. zlib is slower per core than
// libdeflate, so unless the slices spread far enough (useSlicedZlib) the
// stream is one libdeflate pass instead (encodeRgbPngSingle).
[[nodiscard]] ZlibPieces zlibCompressParallel(std::span<const Byte> data, unsigned threads) {
	constexpr std::size_t SLICE = ZLIB_SLICE_BYTES;
	constexpr std::size_t WINDOW = 32uz * 1024;
	constexpr int LEVEL = 6;
	constexpr std::string_view ERROR = "Internal Error: Unable to compress the Reddit carrier PNG.";

	const std::size_t slice_count = std::max<std::size_t>(1, (data.size() + SLICE - 1U) / SLICE);
	struct Slice {
		vBytes deflated;
		uLong adler{};
		std::size_t length{};
	};
	std::vector<Slice> slices(slice_count);
	std::atomic<std::size_t> next{0};
	parallelFor(std::min<std::size_t>(slice_count, std::max(1U, threads)), 1, threads,
		[&](std::size_t, std::size_t) {
			z_stream stream{};
			if (deflateInit2(&stream, LEVEL, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
				throw std::runtime_error(std::string(ERROR));
			}
			struct StreamEnd {
				z_stream& stream;
				~StreamEnd() { deflateEnd(&stream); }
			} stream_end{stream};
			for (std::size_t index; (index = next.fetch_add(1)) < slice_count;) {
				const std::size_t start = index * SLICE;
				const std::size_t length = std::min(SLICE, data.size() - start);
				const bool last = index + 1U == slice_count;
				if (deflateReset(&stream) != Z_OK) {
					throw std::runtime_error(std::string(ERROR));
				}
				if (start > 0) {
					const std::size_t dictionary = std::min(start, WINDOW);
					if (deflateSetDictionary(&stream, data.data() + start - dictionary,
							static_cast<uInt>(dictionary)) != Z_OK) {
						throw std::runtime_error(std::string(ERROR));
					}
				}
				Slice& slice = slices[index];
				// A sync flush adds an empty stored block (5 bytes) to the bound.
				slice.deflated.resize(deflateBound(&stream, static_cast<uLong>(length)) + 16U);
				stream.next_in = const_cast<Byte*>(data.data() + start);
				stream.avail_in = static_cast<uInt>(length);
				stream.next_out = slice.deflated.data();
				stream.avail_out = static_cast<uInt>(slice.deflated.size());
				const int rc = deflate(&stream, last ? Z_FINISH : Z_SYNC_FLUSH);
				if ((last ? rc != Z_STREAM_END : rc != Z_OK) || stream.avail_in != 0) {
					throw std::runtime_error(std::string(ERROR));
				}
				slice.deflated.resize(slice.deflated.size() - stream.avail_out);
				slice.adler = adler32(1, data.data() + start, static_cast<uInt>(length));
				slice.length = length;
			}
		});

	uLong adler = 1;
	ZlibPieces pieces;
	pieces.reserve(slices.size() + 2U);
	pieces.push_back(vBytes{0x78, 0x9C});  // deflate, 32 KiB window, default level
	for (Slice& slice : slices) {
		adler = adler32_combine(adler, slice.adler, static_cast<z_off_t>(slice.length));
		pieces.push_back(std::move(slice.deflated));
	}
	vBytes checksum(4);
	appendBigEndian32(checksum.data(), static_cast<std::uint32_t>(adler));
	pieces.push_back(std::move(checksum));
	return pieces;
}

constexpr std::array<Byte, 8> PNG_SIGNATURE = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
constexpr std::size_t PNG_IHDR_TOTAL = 4 + 4 + 13 + 4;
constexpr std::size_t PNG_MAX_CHUNK_LENGTH = 0x7FFFFFFFuz;
constexpr std::size_t PNG_CHUNK_OVERHEAD = 12;  // length, type and CRC
// Where the zlib stream starts: after the signature, IHDR and the IDAT's
// length and type.
constexpr std::size_t IDAT_DATA_OFFSET = PNG_SIGNATURE.size() + PNG_IHDR_TOTAL + 8;

// Signature and IHDR of the non-interlaced RGB8 output, at `out`.
void writePngHead(Byte* out, const RedditPngCarrier& carrier) noexcept {
	std::memcpy(out, PNG_SIGNATURE.data(), PNG_SIGNATURE.size());
	Byte* ihdr = out + PNG_SIGNATURE.size();
	appendBigEndian32(ihdr, 13);
	std::memcpy(ihdr + 4, "IHDR", 4);
	appendBigEndian32(ihdr + 8, carrier.width);
	appendBigEndian32(ihdr + 12, carrier.height);
	ihdr[16] = 8;  // bit depth
	ihdr[17] = 2;  // colour type: RGB
	ihdr[18] = 0;  // compression
	ihdr[19] = 0;  // filter method
	ihdr[20] = 0;  // no interlace
	appendBigEndian32(ihdr + 21, pdvrdtCrc32Update(0, std::span<const Byte>(ihdr + 4, 4 + 13)));
}

// Bytes the PNG needs around a zlib stream of `zlib_size` bytes: everything
// before IDAT_DATA_OFFSET, the IDAT's CRC and the IEND chunk.
[[nodiscard]] constexpr std::size_t pngSizeAround(std::size_t zlib_size) noexcept {
	return IDAT_DATA_OFFSET + zlib_size + 4U + PNG_CHUNK_OVERHEAD;
}

// Completes a PNG whose head is written and whose `zlib_size`-byte stream sits
// at IDAT_DATA_OFFSET: the IDAT's length and CRC, then IEND, trimming `png`.
// The largest carrier, 8192x8192 RGB8, filters to 201,334,784 bytes, whose
// compression bound is under a tenth of the 2^31-1 chunk limit, so the stream always
// fits one IDAT; the check keeps that from resting on the arithmetic.
void closePng(vBytes& png, std::size_t zlib_size) {
	if (zlib_size > PNG_MAX_CHUNK_LENGTH || png.size() < pngSizeAround(zlib_size)) {
		throw std::runtime_error("Internal Error: Reddit carrier PNG stream exceeds one IDAT chunk.");
	}
	Byte* const idat = png.data() + IDAT_DATA_OFFSET - 8U;
	appendBigEndian32(idat, static_cast<std::uint32_t>(zlib_size));
	std::memcpy(idat + 4, "IDAT", 4);
	Byte* const iend = idat + 8 + zlib_size + 4;
	appendBigEndian32(iend - 4, pdvrdtCrc32Update(0, std::span<const Byte>(idat + 4, 4 + zlib_size)));
	appendBigEndian32(iend, 0);
	std::memcpy(iend + 4, "IEND", 4);
	appendBigEndian32(iend + 8, pdvrdtCrc32Update(0, std::span<const Byte>(iend + 4, 4)));
	png.resize(pngSizeAround(zlib_size));
}

// The unsliced encode: one libdeflate pass compressed straight into the PNG
// buffer, after the IDAT header, so the stream is never held twice.
[[nodiscard]] vBytes encodeRgbPngSingle(const RedditPngCarrier& carrier, vBytes filtered) {
	struct CompressorGuard {
		libdeflate_compressor* compressor = libdeflate_alloc_compressor(6);
		~CompressorGuard() { libdeflate_free_compressor(compressor); }
	} guard;
	if (guard.compressor == nullptr) {
		throw std::bad_alloc();
	}
	const std::size_t bound = libdeflate_zlib_compress_bound(guard.compressor, filtered.size());
	vBytes png(pngSizeAround(bound));
	writePngHead(png.data(), carrier);

	const std::size_t zlib_size = libdeflate_zlib_compress(
		guard.compressor, filtered.data(), filtered.size(), png.data() + IDAT_DATA_OFFSET, bound);
	vBytes{}.swap(filtered);
	if (zlib_size == 0) {
		throw std::runtime_error("Internal Error: Unable to compress the Reddit carrier PNG.");
	}
	closePng(png, zlib_size);
	return png;
}

// The finished carrier as a non-interlaced RGB8 PNG: signature, IHDR, one IDAT
// and IEND, with lodepng's per-row filter choice. Rows are filtered in
// parallel and the zlib stream is compressed in parallel (see
// zlibCompressParallel).
[[nodiscard]] vBytes encodeRgbPng(const RedditPngCarrier& carrier, unsigned threads = workerCount()) {
	constexpr std::string_view ENCODE_ERROR = "Internal Error: Unable to encode the Reddit carrier PNG.";
	const std::size_t line = checkedMulSize(carrier.width, 3, "Image Size Error: Reddit carrier row overflow.");
	const std::size_t filtered_size = checkedMulSize(
		line + 1U, carrier.height, "Image Size Error: Reddit carrier PNG data overflow.");
	if (carrier.width == 0 || carrier.height == 0 || carrier.rgb.size() != line * carrier.height) {
		throw std::runtime_error(std::string(ENCODE_ERROR));
	}

	vBytes filtered(filtered_size);
	const vBytes zero_row(line, 0);
	parallelFor(carrier.height, 64, threads, [&](std::size_t first_row, std::size_t end_row) {
		vBytes scratch(line * 4U);
		const std::array<Byte*, 5> attempt = {
			nullptr, scratch.data(), scratch.data() + line,
			scratch.data() + line * 2U, scratch.data() + line * 3U};
		for (std::size_t y = first_row; y < end_row; ++y) {
			// The zero row stands in for the line above the first scanline.
			const Byte* prev = y == 0 ? zero_row.data() : carrier.rgb.data() + (y - 1U) * line;
			filterRgbRow(carrier.rgb.data() + y * line, prev, line, attempt,
				filtered.data() + y * (line + 1U));
		}
	});

	if (!useSlicedZlib(filtered.size(), threads)) {
		return encodeRgbPngSingle(carrier, std::move(filtered));
	}
	ZlibPieces zlib = zlibCompressParallel(filtered, threads);
	vBytes{}.swap(filtered);
	std::size_t zlib_size = 0;
	for (const vBytes& piece : zlib) {
		zlib_size += piece.size();
	}

	vBytes png(pngSizeAround(zlib_size));
	writePngHead(png.data(), carrier);
	// Copy the stream's pieces into the IDAT, freeing each once it is in place.
	std::size_t at = IDAT_DATA_OFFSET;
	for (vBytes& piece : zlib) {
		std::memcpy(png.data() + at, piece.data(), piece.size());
		at += piece.size();
		vBytes{}.swap(piece);
	}
	closePng(png, zlib_size);
	return png;
}

struct DecodedCarrier {
	RedditPngCarrier carrier{};
	bool fully_opaque{true};
};

[[nodiscard]] unsigned tryDecode(
	const vBytes& png,
	LodePNGColorType colour_type,
	unsigned bit_depth,
	vBytes& pixels,
	unsigned& width,
	unsigned& height) {

	lodepng::State state;
	lodepng_zlib_adapter::configureDecoder(state);
	state.info_raw.colortype = colour_type;
	state.info_raw.bitdepth = bit_depth;
	return lodepng::decode(pixels, width, height, state, png);
}

// True when no pixel of the PNG can be transparent: a greyscale, RGB or
// palette image with no tRNS chunk. Such a cover decodes straight to RGB,
// skipping an RGBA buffer a third larger and the pass that copies out of it.
// Anything else -- an alpha channel, a tRNS chunk, or a file this walk cannot
// read -- takes the RGBA path, which checks every pixel and reports any error.
[[nodiscard]] bool pngCannotHoldTransparency(std::span<const Byte> png) noexcept {
	try {
		const PngChunkView ihdr = readRequiredIhdr(png);
		const Byte colour_type = ihdr.data[9];
		if (colour_type != 0 && colour_type != 2 && colour_type != 3) {
			return false;
		}
		bool has_trns = false;
		forEachChunkToIend(png, ihdr.offset + ihdr.total_size, [&](const PngChunkView& chunk) {
			has_trns = has_trns || chunk.type == TYPE_TRNS;
		});
		return !has_trns;
	} catch (...) {
		return false;
	}
}

// The colour type of a cover whose alpha channel has 16-bit samples, or nullopt.
// An 8-bit decode keeps only each sample's high byte, so alpha 0xFF00-0xFFFE
// would read as 255 and pass as opaque; such a cover is decoded at its own
// precision instead (its native format, so lodepng hands over its own buffer
// rather than converting into a second one).
[[nodiscard]] std::optional<LodePNGColorType> sixteenBitAlphaType(std::span<const Byte> png) noexcept {
	try {
		const PngChunkView ihdr = readRequiredIhdr(png);
		if (ihdr.data[8] != 16) {
			return std::nullopt;
		}
		switch (ihdr.data[9]) {
			case 4: return LCT_GREY_ALPHA;
			case 6: return LCT_RGBA;
			default: return std::nullopt;
		}
	} catch (...) {
		return std::nullopt;
	}
}

[[nodiscard]] DecodedCarrier decodeCarrier(vBytes& png) {
	// Measured note: decoding RGBA straight into the carrier buffer and
	// compacting it to RGB in place was tried, on the theory that it would cut
	// this function's footprint from 7 bytes per pixel to 4. It does not: lodepng
	// holds its own inflate buffer alongside the raw output, and that pair sets
	// the high-water mark. Decoding an opaque cover straight to RGB (below) does
	// save the RGBA buffer, the copy and the alpha scan.
	const bool opaque_by_type = pngCannotHoldTransparency(png);
	const std::optional<LodePNGColorType> alpha16_type = sixteenBitAlphaType(png);
	const LodePNGColorType decode_type = alpha16_type ? *alpha16_type
		: opaque_by_type ? LCT_RGB : LCT_RGBA;
	const unsigned decode_depth = alpha16_type ? 16U : 8U;
	// Bytes per pixel as decoded.
	const std::size_t decoded_channels =
		decode_type == LCT_GREY_ALPHA ? 4U :
		decode_type == LCT_RGBA ? (alpha16_type ? 8U : 4U) :
		3U;
	vBytes pixels_out;
	unsigned width = 0;
	unsigned height = 0;
	unsigned error = tryDecode(png, decode_type, decode_depth, pixels_out, width, height);

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
		pixels_out.clear();
		width = 0;
		height = 0;
		error = tryDecode(png, decode_type, decode_depth, pixels_out, width, height);
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
	const std::size_t expected_size = checkedMulSize(
		pixels, decoded_channels, "Image Size Error: Reddit carrier buffer size overflow.");
	if (pixels_out.size() != expected_size) {
		throw std::runtime_error(
			"Image Decode Error: Reddit carrier has an unexpected pixel buffer size.");
	}

	DecodedCarrier decoded{
		.carrier = RedditPngCarrier{
			.width = width,
			.height = height
		}
	};
	if (alpha16_type) {
		// Big-endian 16-bit samples. Opaque means alpha 0xFFFF exactly; the RGB
		// is each sample's high byte, the same reduction lodepng's 8-bit
		// decode applies, so an opaque cover gets exactly the pixels it did.
		decoded.carrier.rgb.resize(checkedMulSize(
			pixels, 3, "Image Size Error: Reddit RGB buffer size overflow."));
		const bool grey = *alpha16_type == LCT_GREY_ALPHA;
		const Byte* in = pixels_out.data();
		Byte* rgb = decoded.carrier.rgb.data();
		unsigned alpha_and = 0xFFFFU;
		for (std::size_t pixel = 0; pixel < pixels; ++pixel, in += decoded_channels, rgb += 3) {
			rgb[0] = in[0];
			rgb[1] = grey ? in[0] : in[2];
			rgb[2] = grey ? in[0] : in[4];
			const std::size_t alpha_at = grey ? 2 : 6;
			alpha_and &= (static_cast<unsigned>(in[alpha_at]) << 8U) | in[alpha_at + 1];
		}
		decoded.fully_opaque = alpha_and == 0xFFFFU;
	} else if (opaque_by_type) {
		decoded.carrier.rgb = std::move(pixels_out);
	} else {
		decoded.carrier.rgb.resize(checkedMulSize(
			pixels, 3, "Image Size Error: Reddit RGB buffer size overflow."));
		const Byte* rgba = pixels_out.data();
		Byte* rgb = decoded.carrier.rgb.data();
		unsigned alpha_and = 255U;
		for (std::size_t pixel = 0; pixel < pixels; ++pixel, rgba += 4, rgb += 3) {
			rgb[0] = rgba[0];
			rgb[1] = rgba[1];
			rgb[2] = rgba[2];
			alpha_and &= rgba[3];
		}
		decoded.fully_opaque = alpha_and == 255U;
	}

	decoded.carrier.payload_capacity =
		theoreticalCapacity(decoded.carrier.rgb.size());
	return decoded;
}

// A missing magic means the KEY does not locate a carrier here. Once the header
// is identified, bad versions, lengths and CRCs propagate as errors. Returns the
// raw payload; the pdvrdt profile check is the caller's.
[[nodiscard]] std::optional<vBytes> tryExtractPayload(
	const RedditPngCarrier& carrier,
	std::span<const Byte, CarrierKey::BYTES> carrier_key,
	unsigned threads = workerCount()) {

	const KeyedPermutation permutation(carrier.rgb.size(), carrier_key);
	CarrierMasks masks(carrier_key);
	const std::array<Byte, HEADER_SIZE> header = extractHeader(carrier, permutation, masks);
	if (!std::equal(CARRIER_MAGIC.begin(), CARRIER_MAGIC.end(), header.begin())) {
		return std::nullopt;
	}

	validateCarrierHeader(header, carrier.payload_capacity);
	const std::size_t payload_size = readLe32(header.data() + 12);
	masks.preparePayload(payload_size);
	vBytes payload = extractPayload(carrier, permutation, masks, payload_size, threads);
	if (pdvrdtCrc32Update(0, payload) != readLe32(header.data() + 16)) {
		throw std::runtime_error("File Recovery Error: Reddit embedded data is corrupt.");
	}
	return payload;
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

namespace {

// embedRedditPngPayload() on an explicit number of threads; the result does not
// depend on it, which the tests check.
[[nodiscard]] vBytes embedRedditPngPayloadOn(
	RedditPngCarrier& carrier,
	const CarrierKey& carrier_key,
	std::span<const Byte> payload,
	unsigned threads) {

	if (payload.empty()) {
		throw std::runtime_error("Data File Size Error: Reddit carrier payload is empty.");
	}
	if (payload.size() > carrier.payload_capacity) {
		throw std::runtime_error(
			"Data File Size Error: Encrypted Reddit payload exceeds the cover image's theoretical adaptive capacity.");
	}

	const std::array<Byte, HEADER_SIZE> header = makeHeader(payload);
	const KeyedPermutation permutation(carrier.rgb.size(), carrier_key.bytes);
	CarrierMasks masks(carrier_key.bytes);
	masks.preparePayload(payload.size());
	const TieBreakBits ties(carrier_key.bytes, HEADER_SLOTS + payload.size() * 2U * GROUP_SAMPLES);
	{
		// Scoped so the cost map is freed before encoding, the peak.
		const ActivitySums sums = buildActivitySums(carrier, threads);
		const SampleCoster coster(carrier, sums);
		embedHeader(carrier, coster, permutation, masks, ties, header);
		embedPayload(carrier, coster, permutation, masks, ties, payload, threads);
	}
	return encodeRgbPng(carrier, threads);
}

} // namespace

vBytes embedRedditPngPayload(
	RedditPngCarrier& carrier,
	const CarrierKey& carrier_key,
	std::span<const Byte> payload) {
	return embedRedditPngPayloadOn(carrier, carrier_key, payload, workerCount());
}

std::optional<RedditPngCarrier> decodeRedditRecoveryCarrier(vBytes& png) {
	// Recovery is the one path that hands fully untrusted bytes to the PNG
	// decoder -- conceal has already run this check inside its cover preflight,
	// but nothing on the way here has. Without it a few hundred KB of crafted PNG
	// can declare a geometry that drives a multi-gigabyte decode.
	// Only the geometry bound is needed here -- and only it is available to the
	// recover-only web binary, which does not link image.cpp.
	//
	// The decoder sizes its own image-data buffer from its whole input, so
	// everything it would skip is dropped first, in place, and what remains is
	// held proportionate to that geometry. Recovery's 3 GiB input allowance
	// exists for metadata-mode payloads, not for pixel carriers.
	const std::size_t inflated_size = requirePngGeometryWithinDecodeLimit(png);
	retainPixelDecodeChunks(png);
	requireDecodeInputWithinBound(png, inflated_size);

	DecodedCarrier decoded = decodeCarrier(png);
	if (!decoded.fully_opaque || decoded.carrier.rgb.size() <= HEADER_SLOTS) {
		return std::nullopt;
	}
	return std::move(decoded.carrier);
}

std::optional<vBytes> extractRedditPngPayload(
	const RedditPngCarrier& carrier,
	const RecoveryKey& recovery_key) {

	// Derivation failures propagate: an Argon2 allocation error is an
	// operational failure, not a missing carrier.
	CarrierKey carrier_key;
	deriveCarrierKeyFromRecoveryKey(recovery_key, carrier_key);
	auto payload = tryExtractPayload(carrier, carrier_key.bytes);
	if (!payload || !isPdvrdtRedditProfile(*payload)) {
		return std::nullopt;
	}
	return payload;
}
