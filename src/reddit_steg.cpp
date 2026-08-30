#include "reddit_steg.h"

#include "encryption.h"
#include "io_utils.h"
#include "lodepng/lodepng_config.h"
#include "lodepng/lodepng.h"
#include "lodepng/lodepng_zlib_adapter.h"
#include "png_utils.h"

#include <algorithm>
#include <array>
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
// Every sample position and every whitening bit derives from a `carrier_key`
// supplied by the caller, which since v5 is a cheap hash of the recovery PIN
// (see deriveCarrierKeyFromPin). Before v5 this was a hard-coded constant, with
// the consequence that anyone holding the source could recompute the 576 header
// sample positions, undo the whitening and test any image for the PNGSTEG1 magic
// in a few hundred LSB reads -- the carrier was locatable without the PIN.
//
// Keying it from the PIN removes that: without the PIN the positions are
// unknown, so "does this image carry a payload?" stops being answerable by
// anyone but the holder.
//
// This protects *position*, not contents; the payload underneath is
// secretstream-encrypted under the Argon2id key either way.
constexpr std::uint64_t
	PERMUTATION_DOMAIN = 0x9f6abc142d337e51ULL,
	HEADER_DOMAIN      = 0x684452c3a109f5d7ULL,
	PAYLOAD_DOMAIN     = 0x5041594c4f414431ULL,
	DIRECTION_DOMAIN   = 0x444952454354494fULL;

constexpr auto CARRIER_MAGIC = std::to_array<Byte>({
	'P', 'N', 'G', 'S', 'T', 'E', 'G', '1'
});

constexpr Byte
	CARRIER_VERSION = 1,
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

// Eight-round Feistel permutation over the smallest power-of-two domain
// containing the sample count. Cycle walking restricts that permutation to the
// exact RGB-sample range without allocating a carrier-index array.
//
// The halves are allowed to differ in width by one bit, so the domain is always
// the next power of two rather than the next *even* power. Rounding the bit
// count up to an even number (scheme 1) doubled the domain again whenever that
// count was odd, and cycle walking then ran ~4 iterations per lookup instead of
// ~2 -- a cliff an image could fall off by being one pixel wider. Measured on a
// near-full payload, 1183x1183 cost 60% more than 1182x1182 for that reason
// alone.
//
// An odd bit count makes the two halves unequal, so their widths swap on every
// round; the round count is even, which puts them back. The round is still
// invertible (right is carried across unchanged, left is recovered by XORing the
// same round function), so this remains a permutation of the whole domain. Where
// the bit count is even the halves are equal, the swap is a no-op, and the
// result is bit-for-bit what scheme 1 produced.
class KeyedPermutation {
public:
	// Round keys are derived once here rather than inside permutePowerOfTwo():
	// they depend only on the round index and the carrier key, so recomputing
	// them per call cost eight splitmix64 evaluations per Feistel pass on a
	// function invoked once per carrier sample. Worth ~10-14% of recovery, which
	// is dominated by this permutation because it does no cost analysis at all.
	KeyedPermutation(std::uint64_t size, std::uint64_t carrier_key) : size_(size) {
		for (unsigned round = 0; round < round_keys_.size(); ++round) {
			round_keys_[round] = splitmix64(
				carrier_key ^ PERMUTATION_DOMAIN ^
				(static_cast<std::uint64_t>(round) * 0x9e3779b97f4a7c15ULL));
		}

		if (size_ < 2) {
			throw std::runtime_error("Internal Error: Reddit carrier sample domain is too small.");
		}

		unsigned bits = 0;
		std::uint64_t span = size_ - 1;
		while (span != 0) {
			++bits;
			span >>= 1U;
		}
		left_bits_  = bits / 2U;
		right_bits_ = bits - left_bits_;
	}

	[[nodiscard]] std::size_t operator()(std::uint64_t ordinal) const {
		if (ordinal >= size_) {
			throw std::runtime_error("Internal Error: Reddit carrier index is out of range.");
		}
		std::uint64_t value = ordinal;
		do {
			value = permutePowerOfTwo(value);
		} while (value >= size_);
		return static_cast<std::size_t>(value);
	}

private:
	[[nodiscard]] static constexpr std::uint64_t maskFor(unsigned bits) noexcept {
		return (std::uint64_t{1} << bits) - 1U;
	}

	[[nodiscard]] std::uint64_t permutePowerOfTwo(std::uint64_t value) const noexcept {
		unsigned left_bits = left_bits_;
		unsigned right_bits = right_bits_;
		std::uint64_t left = value >> right_bits;
		std::uint64_t right = value & maskFor(right_bits);
		for (unsigned round = 0; round < round_keys_.size(); ++round) {
			const std::uint64_t function = splitmix64(right ^ round_keys_[round]);
			const std::uint64_t next_left = right;                              // right_bits wide
			const std::uint64_t next_right = (left ^ function) & maskFor(left_bits);  // left_bits wide
			left = next_left;
			right = next_right;
			// The halves have exchanged widths; an even round count restores them.
			const unsigned carried_bits = left_bits;
			left_bits = right_bits;
			right_bits = carried_bits;
		}
		return (left << right_bits) | right;
	}

	std::array<std::uint64_t, 8> round_keys_{};
	std::uint64_t size_{};
	unsigned left_bits_{};
	unsigned right_bits_{};

	static_assert(std::tuple_size_v<decltype(round_keys_)> % 2 == 0,
		"permutePowerOfTwo() swaps the half widths every round; an even round "
		"count is what puts them back before the halves are recombined.");
};

[[nodiscard]] std::size_t theoreticalCapacity(std::size_t rgb_samples) noexcept {
	if (rgb_samples <= HEADER_SLOTS) {
		return 0;
	}
	const std::size_t groups = (rgb_samples - HEADER_SLOTS) / GROUP_SAMPLES;
	return groups / 2U;
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

[[nodiscard]] unsigned headerMask(std::uint64_t carrier_key, std::size_t bit_index) noexcept {
	return static_cast<unsigned>(
		splitmix64(
			carrier_key ^ HEADER_DOMAIN ^
			(static_cast<std::uint64_t>(bit_index) * 0x9e3779b97f4a7c15ULL)) &
		1U);
}

[[nodiscard]] unsigned payloadMask(std::uint64_t carrier_key, std::size_t group_index) noexcept {
	return static_cast<unsigned>(
		splitmix64(
			carrier_key ^ PAYLOAD_DOMAIN ^
			(static_cast<std::uint64_t>(group_index) * 0x9e3779b97f4a7c15ULL)) &
		0x0fU);
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
	const auto directional_score = [](int center, int first, int second) {
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
	std::uint64_t carrier_key,
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
		} else if (cost == best.cost &&
			(splitmix64(carrier_key ^ DIRECTION_DOMAIN ^ sample) & 1U) != 0U) {
			best = {.cost = cost, .value = candidate};
		}
	}
	return best;
}

void setParity(
	RedditPngCarrier& carrier,
	std::span<const Byte> activity,
	std::uint64_t carrier_key,
	std::size_t sample,
	unsigned target) {

	if ((carrier.rgb[sample] & 1U) != target) {
		carrier.rgb[sample] = chooseChange(carrier, activity, carrier_key, sample).value;
	}
}

void embedHeader(
	RedditPngCarrier& carrier,
	std::span<const Byte> activity,
	const KeyedPermutation& permutation,
	std::uint64_t carrier_key,
	const std::array<Byte, HEADER_SIZE>& header) {

	for (std::size_t bit = 0; bit < HEADER_BITS; ++bit) {
		const unsigned target = headerBit(header, bit) ^ headerMask(carrier_key, bit);
		for (std::size_t copy = 0; copy < HEADER_COPIES; ++copy) {
			const std::size_t ordinal = bit * HEADER_COPIES + copy;
			setParity(carrier, activity, carrier_key, permutation(ordinal), target);
		}
	}
}

void embedPayload(
	RedditPngCarrier& carrier,
	std::span<const Byte> activity,
	const KeyedPermutation& permutation,
	std::uint64_t carrier_key,
	std::span<const Byte> payload) {

	const std::size_t nibble_count = checkedMulSize(
		payload.size(), 2, "Data File Size Error: Reddit payload size overflow.");
	const std::size_t row_bytes = static_cast<std::size_t>(carrier.width) * 3U;

	for (std::size_t group = 0; group < nibble_count; ++group) {
		std::array<std::size_t, GROUP_SAMPLES> samples{};
		std::array<ChangeChoice, GROUP_SAMPLES> choices{};

		// This loop is memory-bound, not arithmetic-bound. The permutation
		// deliberately scatters a group's 15 samples across the whole image, so
		// on any cover larger than cache each chooseChange() below stalls on
		// cold lines -- substituting an identity permutation (which destroys the
		// format, so purely as a measurement) cut conceal time roughly in half.
		//
		// Resolving all 15 indices up front and prefetching their pixel
		// neighbourhoods lets those independent misses overlap instead of
		// serialising. Format-neutral: the samples, the order they are visited
		// and every value written are unchanged. Measured ~26% off conceal.
		//
		// Things that were tried here and did NOT help, so they are not worth
		// re-attempting: precomputing the 3x3 smoothed-activity map once per
		// pixel rather than once per channel, and deferring the 15
		// chooseChange() calls until delta is known to be non-zero (one group in
		// sixteen discards all 15). Both are output-preserving and both measured
		// as no change -- the arithmetic they remove is not the bottleneck.
		for (std::size_t position = 0; position < GROUP_SAMPLES; ++position) {
			const std::size_t ordinal =
				HEADER_SLOTS + group * GROUP_SAMPLES + position;
			const std::size_t sample = permutation(ordinal);
			samples[position] = sample;

			const char* const base =
				reinterpret_cast<const char*>(carrier.rgb.data()) + sample;
			__builtin_prefetch(base, 0, 1);
			if (sample >= row_bytes) {
				__builtin_prefetch(base - row_bytes, 0, 1);
			}
			if (sample + row_bytes < carrier.rgb.size()) {
				__builtin_prefetch(base + row_bytes, 0, 1);
			}
			__builtin_prefetch(activity.data() + (sample / 3U), 0, 1);
		}

		unsigned syndrome = 0;
		for (std::size_t position = 0; position < GROUP_SAMPLES; ++position) {
			if ((carrier.rgb[samples[position]] & 1U) != 0U) {
				syndrome ^= static_cast<unsigned>(position + 1U);
			}
			choices[position] = chooseChange(carrier, activity, carrier_key, samples[position]);
		}

		const unsigned nibble = (group & 1U) == 0U
			? payload[group / 2U] >> 4U
			: payload[group / 2U] & 0x0fU;
		const unsigned delta = syndrome ^ nibble ^ payloadMask(carrier_key, group);
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

[[nodiscard]] std::array<Byte, HEADER_SIZE> extractHeader(
	const RedditPngCarrier& carrier,
	const KeyedPermutation& permutation,
	std::uint64_t carrier_key) {

	std::array<Byte, HEADER_SIZE> header{};
	for (std::size_t bit = 0; bit < HEADER_BITS; ++bit) {
		unsigned ones = 0;
		for (std::size_t copy = 0; copy < HEADER_COPIES; ++copy) {
			const std::size_t ordinal = bit * HEADER_COPIES + copy;
			ones += carrier.rgb[permutation(ordinal)] & 1U;
		}
		const unsigned whitened = ones > HEADER_COPIES / 2U ? 1U : 0U;
		setHeaderBit(header, bit, whitened ^ headerMask(carrier_key, bit));
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

[[nodiscard]] vBytes extractPayload(
	const RedditPngCarrier& carrier,
	const KeyedPermutation& permutation,
	std::uint64_t carrier_key,
	std::size_t payload_size) {

	vBytes payload(payload_size, 0);
	const std::size_t nibble_count = checkedMulSize(
		payload_size,
		2,
		"File Recovery Error: Reddit payload size overflow.");
	for (std::size_t group = 0; group < nibble_count; ++group) {
		unsigned syndrome = 0;
		for (std::size_t position = 0; position < GROUP_SAMPLES; ++position) {
			const std::size_t ordinal =
				HEADER_SLOTS + group * GROUP_SAMPLES + position;
			const std::size_t sample = permutation(ordinal);
			if ((carrier.rgb[sample] & 1U) != 0U) {
				syndrome ^= static_cast<unsigned>(position + 1U);
			}
		}

		const unsigned nibble = syndrome ^ payloadMask(carrier_key, group);
		if ((group & 1U) == 0U) {
			payload[group / 2U] = static_cast<Byte>(nibble << 4U);
		} else {
			payload[group / 2U] |= static_cast<Byte>(nibble);
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
	std::span<const Byte> png,
	vBytes& rgba,
	unsigned& width,
	unsigned& height) {

	lodepng::State state;
	lodepng_zlib_adapter::configureDecoder(state);
	state.info_raw.colortype = LCT_RGBA;
	state.info_raw.bitdepth = 8;
	return lodepng::decode(rgba, width, height, state, png.data(), png.size());
}

[[nodiscard]] DecodedCarrier decodeCarrier(std::span<const Byte> png) {
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
	vBytes repaired;
	if (error == lodepng_zlib_adapter::ERROR_TRAILING_DATA) {
		repaired.assign(png.begin(), png.end());
		if (stripTrailingIdatData(repaired)) {
			rgba.clear();
			width = 0;
			height = 0;
			error = tryDecodeRgba(repaired, rgba, width, height);
		}
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

} // namespace

RedditPngCarrier prepareRedditPngCarrier(std::span<const Byte> png) {
	DecodedCarrier decoded = decodeCarrier(png);
	if (!decoded.fully_opaque) {
		throw std::runtime_error(
			"Image Format Error: Reddit cover contains transparency; flatten it first.");
	}
	return std::move(decoded.carrier);
}

vBytes embedRedditPngPayload(
	RedditPngCarrier& carrier,
	std::uint64_t carrier_key,
	std::span<const Byte> payload) {

	if (payload.empty()) {
		throw std::runtime_error("Data File Size Error: Reddit carrier payload is empty.");
	}
	if (payload.size() > carrier.payload_capacity) {
		throw std::runtime_error(
			"Data File Size Error: Encrypted Reddit payload exceeds the cover image's theoretical adaptive capacity.");
	}

	const std::array<Byte, HEADER_SIZE> header = makeHeader(payload);
	const KeyedPermutation permutation(carrier.rgb.size(), carrier_key);
	const vBytes activity = buildActivityMap(carrier);
	embedHeader(carrier, activity, permutation, carrier_key, header);
	embedPayload(carrier, activity, permutation, carrier_key, payload);
	return encodeRgbPng(carrier);
}

std::optional<vBytes> extractRedditPngPayload(
	std::span<const Byte> png,
	std::uint64_t carrier_key) {
	// Recovery is the one path that hands fully untrusted bytes to the PNG
	// decoder -- conceal has already run this check inside optimizeImage(), but
	// nothing on the way here has. Without it a few hundred KB of crafted PNG
	// can declare a geometry that drives a multi-gigabyte decode before any PIN
	// is asked for or any authentication happens.
	// Only the geometry bound is needed here -- and only it is available to the
	// recover-only web binary, which does not link image.cpp.
	requirePngGeometryWithinDecodeLimit(png);

	DecodedCarrier decoded = decodeCarrier(png);
	if (!decoded.fully_opaque ||
		decoded.carrier.rgb.size() <= HEADER_SLOTS) {
		return std::nullopt;
	}

	const KeyedPermutation permutation(decoded.carrier.rgb.size(), carrier_key);
	const std::array<Byte, HEADER_SIZE> header =
		extractHeader(decoded.carrier, permutation, carrier_key);
	if (!std::equal(
			CARRIER_MAGIC.begin(),
			CARRIER_MAGIC.end(),
			header.begin())) {
		return std::nullopt;
	}

	validateCarrierHeader(header, decoded.carrier.payload_capacity);
	const std::size_t payload_size = readLe32(header.data() + 12);
	vBytes payload =
		extractPayload(decoded.carrier, permutation, carrier_key, payload_size);
	const std::uint32_t expected_crc = readLe32(header.data() + 16);
	const std::uint32_t actual_crc = pdvrdtCrc32Update(0, payload);
	if (expected_crc != actual_crc) {
		throw std::runtime_error(
			"File Recovery Error: Reddit embedded data is corrupt.");
	}
	if (!isPdvrdtRedditProfile(payload)) {
		return std::nullopt;
	}
	return payload;
}
