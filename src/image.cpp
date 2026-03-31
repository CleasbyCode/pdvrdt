#include "image.h"
#include "encryption.h"
#include "io_utils.h"
#include "lodepng/lodepng_config.h"
#include "lodepng/lodepng.h"
#include "lodepng/lodepng_zlib_adapter.h"
#include "png_utils.h"

#include <cstring>
#include <format>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace {

constexpr Byte
	INDEXED_PLTE   = 3,
	TRUECOLOR_RGB  = 2,
	TRUECOLOR_RGBA = 6;

constexpr std::size_t DEFAULT_PDV_SIG_OFFSET = 101;

[[nodiscard]] constexpr std::uint32_t packRgbaKey(Byte red, Byte green, Byte blue, Byte alpha) {
	return
		(static_cast<std::uint32_t>(red) << 24)   |
		(static_cast<std::uint32_t>(green) << 16) |
		(static_cast<std::uint32_t>(blue) << 8)   |
		 static_cast<std::uint32_t>(alpha);
}

[[nodiscard]] std::size_t checkedMulSize(std::size_t lhs, std::size_t rhs, std::string_view message) {
	if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
		throw std::runtime_error(std::string(message));
	}
	return lhs * rhs;
}

void convertToPalette(vBytes& image_file_vec, const vBytes& image, unsigned width, unsigned height, const LodePNGColorStats& stats, LodePNGColorType raw_color_type) {

	constexpr Byte
		PALETTE_BIT_DEPTH = 8,
		ALPHA_OPAQUE      = 255;

	constexpr std::size_t
		RGBA_COMPONENTS  = 4,
		RGB_COMPONENTS   = 3,
		MAX_PALETTE_SIZE = 256;

	// Validate color type — this function only handles RGB and RGBA input.
	if (raw_color_type != LCT_RGB && raw_color_type != LCT_RGBA) {
		throw std::runtime_error(std::format(
			"convertToPalette: Unsupported color type {}. Expected RGB or RGBA.",
			static_cast<unsigned>(raw_color_type)));
	}

	const std::size_t palette_size = stats.numcolors;

	if (palette_size == 0) {
		throw std::runtime_error("convertToPalette: Palette is empty.");
	}
	if (palette_size > MAX_PALETTE_SIZE) {
		throw std::runtime_error(std::format(
			"convertToPalette: Palette has {} colors, exceeds maximum of {}.",
			palette_size, MAX_PALETTE_SIZE));
	}

	const std::size_t channels =
		(raw_color_type == LCT_RGBA) ? RGBA_COMPONENTS : RGB_COMPONENTS;

	std::unordered_map<std::uint32_t, Byte> color_to_index;
	color_to_index.reserve(palette_size);

	for (std::size_t i = 0; i < palette_size; ++i) {
		const Byte* src = &stats.palette[i * RGBA_COMPONENTS];
		color_to_index.emplace(packRgbaKey(src[0], src[1], src[2], src[3]), static_cast<Byte>(i));
	}

	// Map each pixel to its palette index.
	const std::size_t pixel_count = checkedMulSize(
		static_cast<std::size_t>(width),
		static_cast<std::size_t>(height),
		"Image Error: Pixel count overflow."
	);
	const std::size_t expected_image_size = checkedMulSize(
		pixel_count,
		channels,
		"Image Error: Decoded image size overflow."
	);
	if (image.size() != expected_image_size) {
		throw std::runtime_error("Image Error: Decoded image size does not match PNG dimensions.");
	}
	vBytes indexed_image(pixel_count);

	const bool has_alpha = (raw_color_type == LCT_RGBA);
	for (std::size_t i = 0; i < pixel_count; ++i) {
		const std::size_t offset = i * channels;
		const std::uint32_t key = packRgbaKey(
			image[offset],
			image[offset + 1],
			image[offset + 2],
			has_alpha ? image[offset + 3] : ALPHA_OPAQUE
		);

		const auto it = color_to_index.find(key);
		if (it == color_to_index.end()) {
			throw std::runtime_error(std::format(
				"convertToPalette: Pixel {} has color 0x{:08X} not found in palette.",
				i, key));
		}
		indexed_image[i] = it->second;
	}

	// Encode as 8-bit palette PNG.
	lodepng::State encode_state;
	lodepng_zlib_adapter::configureEncoder(encode_state);
	encode_state.info_raw.colortype       = LCT_PALETTE;
	encode_state.info_raw.bitdepth        = PALETTE_BIT_DEPTH;
	encode_state.info_png.color.colortype = LCT_PALETTE;
	encode_state.info_png.color.bitdepth  = PALETTE_BIT_DEPTH;
	encode_state.encoder.auto_convert     = 0;

	for (std::size_t i = 0; i < palette_size; ++i) {
		const Byte* p = &stats.palette[i * RGBA_COMPONENTS];
		lodepng_palette_add(&encode_state.info_png.color, p[0], p[1], p[2], p[3]);
		lodepng_palette_add(&encode_state.info_raw, p[0], p[1], p[2], p[3]);
	}

	vBytes output;
	unsigned error = lodepng::encode(output, indexed_image.data(), width, height, encode_state);
	if (error) {
		throw std::runtime_error(std::format("LodePNG encode error: {}", error));
	}
	image_file_vec = std::move(output);
}

// ============================================================================
// Internal: Strip non-essential chunks, keeping only IHDR, PLTE, tRNS, IDAT, IEND
// ============================================================================

void stripAndCopyChunks(vBytes& image_file_vec, Byte color_type) {
	constexpr auto PNG_SIG = std::to_array<Byte>({
		0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
	});
	constexpr auto PDVRDT_IDAT_PREFIX = std::to_array<Byte>({ 0x78, 0x5E, 0x5C });
	constexpr auto PDVRDT_SIG = std::to_array<Byte>({ 0xC6, 0x50, 0x3C, 0xEA, 0x5E, 0x9D, 0xF9 });
	constexpr auto KDF_MAGIC = std::to_array<Byte>({ 'K', 'D', 'F', '2' });
	constexpr std::uint32_t TYPE_IHDR = 0x49484452u;
	constexpr std::uint32_t TYPE_PLTE = 0x504C5445u;
	constexpr std::uint32_t TYPE_TRNS = 0x74524E53u;
	constexpr std::uint32_t TYPE_IDAT = 0x49444154u;
	constexpr std::uint32_t TYPE_IEND = 0x49454E44u;

	constexpr std::size_t
		PNG_HEADER_SIZE      = 8,
		IHDR_DATA_SIZE       = 13,
		CHUNK_OVERHEAD       = 12, // length(4) + type(4) + crc(4)
		MIN_PNG_SIZE         = PNG_HEADER_SIZE + CHUNK_OVERHEAD + IHDR_DATA_SIZE + CHUNK_OVERHEAD;

	auto looksLikePdvrdtIdat = [&](std::span<const Byte> chunk_data) {
		if (!spanHasRange(chunk_data, 0, PDVRDT_IDAT_PREFIX.size())) {
			return false;
		}
		if (!std::ranges::equal(chunk_data.subspan(0, PDVRDT_IDAT_PREFIX.size()), PDVRDT_IDAT_PREFIX)) {
			return false;
		}

		const std::size_t profile_offset = PDVRDT_IDAT_PREFIX.size();
		const std::span<const Byte> profile = chunk_data.subspan(profile_offset);
		return
			spanHasRange(profile, DEFAULT_OFFSETS.kdf_metadata, KDF_METADATA_REGION_BYTES) &&
			spanHasRange(profile, DEFAULT_PDV_SIG_OFFSET, PDVRDT_SIG.size()) &&
			std::ranges::equal(
				profile.subspan(DEFAULT_OFFSETS.kdf_metadata + KDF_MAGIC_OFFSET, KDF_MAGIC.size()),
				KDF_MAGIC
			) &&
			profile[DEFAULT_OFFSETS.kdf_metadata + KDF_ALG_OFFSET] == KDF_ALG_ARGON2ID13 &&
			profile[DEFAULT_OFFSETS.kdf_metadata + KDF_SENTINEL_OFFSET] == KDF_SENTINEL &&
			std::ranges::equal(profile.subspan(DEFAULT_PDV_SIG_OFFSET, PDVRDT_SIG.size()), PDVRDT_SIG);
	};

	if (image_file_vec.size() < MIN_PNG_SIZE) {
		throw std::runtime_error("PNG Error: File too small to contain valid PNG structure.");
	}
	if (std::memcmp(image_file_vec.data(), PNG_SIG.data(), PNG_HEADER_SIZE) != 0) {
		throw std::runtime_error("PNG Error: Invalid PNG signature.");
	}

	std::size_t pos = PNG_HEADER_SIZE;
	requireSpanRange(image_file_vec, pos, 8, "PNG Error: Corrupt IHDR chunk header.");
	const std::size_t ihdr_len = getValue(image_file_vec, pos, 4);
	const std::uint32_t ihdr_type = static_cast<std::uint32_t>(getValue(image_file_vec, pos + 4, 4));
	if (ihdr_len != IHDR_DATA_SIZE || ihdr_type != TYPE_IHDR) {
		throw std::runtime_error("PNG Error: Missing or corrupt IHDR chunk.");
	}
	const std::size_t ihdr_chunk_size = ihdr_len + CHUNK_OVERHEAD;
	requireSpanRange(image_file_vec, pos, ihdr_chunk_size, "PNG Error: Corrupt IHDR chunk length.");

	pos += ihdr_chunk_size;
	std::size_t write_pos = pos;

	bool has_iend = false;
	while (pos < image_file_vec.size()) {
		requireSpanRange(image_file_vec, pos, 8, "PNG Error: Corrupt PNG chunk header.");
		const std::size_t chunk_len = getValue(image_file_vec, pos, 4);
		const std::size_t type_index = pos + 4;
		const std::size_t data_index = type_index + 4;
		if (chunk_len > image_file_vec.size() - data_index || 4 > image_file_vec.size() - (data_index + chunk_len)) {
			throw std::runtime_error("PNG Error: Corrupt PNG chunk length.");
		}
		const std::size_t crc_index = data_index + chunk_len;
		const std::size_t chunk_size = chunk_len + CHUNK_OVERHEAD;
		const std::uint32_t chunk_type = static_cast<std::uint32_t>(getValue(image_file_vec, type_index, 4));

		const std::span<const Byte> chunk_data(
			image_file_vec.data() + static_cast<std::ptrdiff_t>(data_index),
			chunk_len
		);

		const std::uint32_t stored_crc = static_cast<std::uint32_t>(getValue(image_file_vec, crc_index, 4));
		const std::uint32_t computed_crc = lodepng_crc32(
			image_file_vec.data() + static_cast<std::ptrdiff_t>(type_index),
			chunk_len + 4
		);
		if (stored_crc != computed_crc) {
			throw std::runtime_error("PNG Error: Corrupt PNG chunk CRC.");
		}

		const bool keep_chunk =
			(chunk_type == TYPE_PLTE && color_type == INDEXED_PLTE) ||
			chunk_type == TYPE_TRNS ||
			(chunk_type == TYPE_IDAT && !looksLikePdvrdtIdat(chunk_data)) ||
			chunk_type == TYPE_IEND;
		if (keep_chunk) {
			if (write_pos != pos) {
				std::memmove(image_file_vec.data() + write_pos, image_file_vec.data() + pos, chunk_size);
			}
			write_pos += chunk_size;
		}

		pos += chunk_size;
		if (chunk_type == TYPE_IEND) {
			has_iend = true;
			break;
		}
	}

	if (!has_iend) {
		throw std::runtime_error("PNG Error: Missing IEND chunk.");
	}
	image_file_vec.resize(write_pos);
}

} // namespace

// ============================================================================
// Public: Optimize image for polyglot embedding
// ============================================================================

bool optimizeImage(vBytes& image_file_vec) {
	constexpr uint16_t
		MIN_DIMS       = 68,
		MAX_PLTE_DIMS  = 4096,
		MAX_RGB_DIMS   = 900,
		MIN_RGB_COLORS = 257;

	lodepng::State state;
	lodepng_zlib_adapter::configureDecoder(state);
	// Note: ignore_adler32 has no effect when using custom_zlib callbacks.
	// System zlib handles checksum validation internally.

	vBytes image;
	unsigned
		width = 0,
		height = 0,
		error = lodepng::decode(image, width, height, state, image_file_vec);
	if (error) {
		throw std::runtime_error(std::format("LodePNG decode error {}: {}", error, lodepng_error_text(error)));
	}

	LodePNGColorStats stats;
	lodepng_color_stats_init(&stats);
	stats.allow_palette   = 1;
	stats.allow_greyscale = 0;
	error = lodepng_compute_color_stats(&stats, image.data(), width, height, &state.info_raw);
	if (error) {
		throw std::runtime_error(std::format("LodePNG stats error: {}", error));
	}

	const Byte color_type = static_cast<Byte>(state.info_png.color.colortype);
	const bool is_truecolor = (color_type == TRUECOLOR_RGB || color_type == TRUECOLOR_RGBA);
	const bool can_palettize = is_truecolor && (stats.numcolors < MIN_RGB_COLORS);

	auto check_dims = [&](uint16_t max_dim) -> bool {
		return width < MIN_DIMS || height < MIN_DIMS || width > max_dim || height > max_dim;
	};

	if (can_palettize) {
		convertToPalette(image_file_vec, image, width, height, stats, state.info_raw.colortype);
		return check_dims(MAX_PLTE_DIMS);
	}

	const uint16_t max_dim = (color_type == INDEXED_PLTE) ? MAX_PLTE_DIMS : MAX_RGB_DIMS;
	stripAndCopyChunks(image_file_vec, color_type);
	return check_dims(max_dim);
}
