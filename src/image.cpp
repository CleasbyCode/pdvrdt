#include "image.h"
#include "encryption.h"
#include "io_utils.h"
#include "lodepng/lodepng_config.h"
#include "lodepng/lodepng.h"
#include "lodepng/lodepng_zlib_adapter.h"
#include "png_utils.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

constexpr Byte
	INDEXED_PLTE   = 3,
	TRUECOLOR_RGB  = 2,
	TRUECOLOR_RGBA = 6;

constexpr std::uint32_t
	TYPE_IHDR = 0x49484452u,
	TYPE_PLTE = 0x504C5445u,
	TYPE_TRNS = 0x74524E53u,
	TYPE_IDAT = 0x49444154u,
	TYPE_IEND = 0x49454E44u;

constexpr std::size_t
	PNG_HEADER_SIZE          = 8,
	IHDR_DATA_SIZE           = 13,
	CHUNK_OVERHEAD           = 12,
	MAX_LODEPNG_DECODE_BYTES = 256ULL * 1024 * 1024;

[[nodiscard]] constexpr std::uint32_t packRgbaKey(Byte red, Byte green, Byte blue, Byte alpha) {
	return
		(static_cast<std::uint32_t>(red) << 24)   |
		(static_cast<std::uint32_t>(green) << 16) |
		(static_cast<std::uint32_t>(blue) << 8)   |
		 static_cast<std::uint32_t>(alpha);
}

[[nodiscard]] constexpr std::uint32_t packRgbKey(Byte red, Byte green, Byte blue) {
	return
		(static_cast<std::uint32_t>(red) << 16) |
		(static_cast<std::uint32_t>(green) << 8) |
		 static_cast<std::uint32_t>(blue);
}

struct PaletteEntry {
	std::uint32_t key{};
	Byte index{};
};

struct DecodedImageForOptimization {
	vBytes image{};
	unsigned width{};
	unsigned height{};
	LodePNGColorStats stats{};
	Byte png_color_type{};
	LodePNGColorType raw_color_type{};
};

constexpr std::uint16_t PALETTE_INDEX_MISSING = 0xFFFFu;

[[nodiscard]] unsigned channelsForPngColorType(Byte color_type) {
	switch (color_type) {
		case 0: return 1; // greyscale
		case 2: return 3; // RGB
		case 3: return 1; // indexed palette
		case 4: return 2; // greyscale + alpha
		case 6: return 4; // RGBA
		default:
			throw std::runtime_error("PNG Error: Unsupported PNG color type.");
	}
}

[[nodiscard]] bool isValidBitDepthForColorType(Byte bit_depth, Byte color_type) {
	switch (color_type) {
		case 0: return bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8 || bit_depth == 16;
		case 2: return bit_depth == 8 || bit_depth == 16;
		case 3: return bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8;
		case 4: return bit_depth == 8 || bit_depth == 16;
		case 6: return bit_depth == 8 || bit_depth == 16;
		default: return false;
	}
}

[[nodiscard]] bool looksLikePdvrdtIdat(std::span<const Byte> chunk_data) {
	constexpr auto PDVRDT_IDAT_PREFIX = std::to_array<Byte>({ 0x78, 0x5E, 0x5C });

	if (!spanHasRange(chunk_data, 0, PDVRDT_IDAT_PREFIX.size())) {
		return false;
	}
	if (!std::ranges::equal(chunk_data.subspan(0, PDVRDT_IDAT_PREFIX.size()), PDVRDT_IDAT_PREFIX)) {
		return false;
	}

	const std::span<const Byte> profile = chunk_data.subspan(PDVRDT_IDAT_PREFIX.size());
	return
		hasSupportedKdfMetadataAt(profile, DEFAULT_OFFSETS.kdf_metadata) &&
		spanHasRange(profile, DEFAULT_PDV_SIG_OFFSET, PDVRDT_SIG.size()) &&
		std::ranges::equal(profile.subspan(DEFAULT_PDV_SIG_OFFSET, PDVRDT_SIG.size()), PDVRDT_SIG);
}

[[nodiscard]] PngChunkView readRequiredIhdr(std::span<const Byte> png) {
	requirePngSignature(png, "PNG Error: Invalid PNG signature.");

	const PngChunkView ihdr = readPngChunk(
		png,
		PNG_HEADER_SIZE,
		"PNG Error: Corrupt IHDR chunk header.",
		"PNG Error: Corrupt IHDR chunk length.",
		"PNG Error: Corrupt IHDR chunk CRC."
	);
	if (ihdr.length != IHDR_DATA_SIZE || ihdr.type != TYPE_IHDR) {
		throw std::runtime_error("PNG Error: Missing or corrupt IHDR chunk.");
	}
	return ihdr;
}

template <typename KeepChunk>
void compactChunksAfterIhdr(vBytes& image_file_vec, std::size_t first_chunk_pos, KeepChunk&& keep_chunk) {
	std::size_t pos = first_chunk_pos;
	std::size_t write_pos = pos;
	bool has_iend = false;

	while (pos < image_file_vec.size()) {
		const PngChunkView chunk = readPngChunk(
			image_file_vec,
			pos,
			"PNG Error: Corrupt PNG chunk header.",
			"PNG Error: Corrupt PNG chunk length.",
			"PNG Error: Corrupt PNG chunk CRC."
		);
		if (chunk.type == TYPE_IEND && chunk.length != 0) {
			throw std::runtime_error("PNG Error: Corrupt PNG structure. Invalid IEND.");
		}

		if (keep_chunk(chunk)) {
			if (write_pos != pos) {
				std::memmove(image_file_vec.data() + write_pos, image_file_vec.data() + pos, chunk.total_size);
			}
			write_pos += chunk.total_size;
		}

		pos += chunk.total_size;
		if (chunk.type == TYPE_IEND) {
			has_iend = true;
			break;
		}
	}

	if (!has_iend) {
		throw std::runtime_error("PNG Error: Missing IEND chunk.");
	}
	image_file_vec.resize(write_pos);
}

void preflightPngDecode(std::span<const Byte> png) {
	requireSpanRange(png, 0, PNG_HEADER_SIZE + CHUNK_OVERHEAD + IHDR_DATA_SIZE, "PNG Error: File too small to contain valid PNG structure.");
	const PngChunkView ihdr = readRequiredIhdr(png);

	const std::uint32_t width = static_cast<std::uint32_t>(getValue(ihdr.data, 0, 4));
	const std::uint32_t height = static_cast<std::uint32_t>(getValue(ihdr.data, 4, 4));
	const Byte bit_depth = ihdr.data[8];
	const Byte color_type = ihdr.data[9];
	const Byte compression_method = ihdr.data[10];
	const Byte filter_method = ihdr.data[11];
	const Byte interlace_method = ihdr.data[12];

	if (width == 0 || height == 0 ||
		compression_method != 0 ||
		filter_method != 0 ||
		(interlace_method != 0 && interlace_method != 1) ||
		!isValidBitDepthForColorType(bit_depth, color_type)) {
		throw std::runtime_error("PNG Error: Invalid IHDR metadata.");
	}

	const std::size_t pixel_count = checkedMulSize(width, height, "PNG Error: Pixel count overflow.");
	const std::size_t decoded_rgba_size = checkedMulSize(pixel_count, 4, "PNG Error: Decoded image size overflow.");
	if (decoded_rgba_size > MAX_LODEPNG_DECODE_BYTES) {
		throw std::runtime_error("PNG Error: Decoded image exceeds safety limit.");
	}

	const std::size_t bits_per_pixel = checkedMulSize(
		static_cast<std::size_t>(channelsForPngColorType(color_type)),
		static_cast<std::size_t>(bit_depth),
		"PNG Error: Scanline size overflow."
	);
	const std::size_t row_bits = checkedMulSize(
		static_cast<std::size_t>(width),
		bits_per_pixel,
		"PNG Error: Scanline size overflow."
	);
	const std::size_t row_bytes = checkedAddSize(row_bits / 8, (row_bits % 8) ? 1 : 0, "PNG Error: Scanline size overflow.");
	const std::size_t filtered_row_bytes = checkedAddSize(row_bytes, 1, "PNG Error: Scanline size overflow.");
	const std::size_t estimated_inflated_size = checkedMulSize(
		filtered_row_bytes,
		static_cast<std::size_t>(height),
		"PNG Error: Inflated image size overflow."
	);
	if (estimated_inflated_size > MAX_LODEPNG_DECODE_BYTES) {
		throw std::runtime_error("PNG Error: Inflated image exceeds safety limit.");
	}
}

void removeExistingPdvrdtIdatChunks(vBytes& image_file_vec) {
	requireSpanRange(image_file_vec, 0, PNG_HEADER_SIZE, "PNG Error: Invalid PNG signature.");
	const PngChunkView ihdr = readRequiredIhdr(image_file_vec);
	compactChunksAfterIhdr(image_file_vec, ihdr.offset + ihdr.total_size, [](const PngChunkView& chunk) {
		return chunk.type != TYPE_IDAT || !looksLikePdvrdtIdat(chunk.data);
	});
}

[[nodiscard]] std::vector<PaletteEntry> buildSortedPaletteIndex(const LodePNGColorStats& stats, std::size_t palette_size) {
	constexpr std::size_t RGBA_COMPONENTS = 4;

	std::vector<PaletteEntry> entries;
	entries.resize(palette_size);

	for (std::size_t i = 0; i < palette_size; ++i) {
		const Byte* src = &stats.palette[i * RGBA_COMPONENTS];
		entries[i] = PaletteEntry{
			.key = packRgbaKey(src[0], src[1], src[2], src[3]),
			.index = static_cast<Byte>(i)
		};
	}

	std::ranges::sort(entries, {}, &PaletteEntry::key);
	return entries;
}

[[nodiscard]] std::uint16_t lookupSortedPaletteIndex(std::span<const PaletteEntry> entries, std::uint32_t key) {
	const auto it = std::ranges::lower_bound(entries, key, {}, &PaletteEntry::key);
	if (it == entries.end() || it->key != key) {
		return PALETTE_INDEX_MISSING;
	}
	return it->index;
}

[[nodiscard]] bool paletteIsOpaque(const LodePNGColorStats& stats, std::size_t palette_size) {
	constexpr std::size_t
		RGBA_COMPONENTS = 4,
		ALPHA_OFFSET = 3;
	constexpr Byte ALPHA_OPAQUE = 255;

	for (std::size_t i = 0; i < palette_size; ++i) {
		if (stats.palette[i * RGBA_COMPONENTS + ALPHA_OFFSET] != ALPHA_OPAQUE) {
			return false;
		}
	}
	return true;
}

void mapRgbPixelsToPaletteDirect(
	vBytes& indexed_image,
	const vBytes& image,
	const LodePNGColorStats& stats,
	std::size_t palette_size,
	std::size_t channels,
	bool has_alpha) {

	constexpr std::size_t
		RGBA_COMPONENTS = 4,
		ALPHA_OFFSET = 3,
		RGB_LOOKUP_TABLE_SIZE = 1ULL << 24;
	constexpr Byte ALPHA_OPAQUE = 255;

	std::vector<std::uint16_t> color_to_index(RGB_LOOKUP_TABLE_SIZE, PALETTE_INDEX_MISSING);

	for (std::size_t i = 0; i < palette_size; ++i) {
		const Byte* src = &stats.palette[i * RGBA_COMPONENTS];
		const std::uint32_t key = packRgbKey(src[0], src[1], src[2]);
		color_to_index[static_cast<std::size_t>(key)] = static_cast<std::uint16_t>(i);
	}

	for (std::size_t i = 0; i < indexed_image.size(); ++i) {
		const std::size_t offset = i * channels;
		if (has_alpha && image[offset + ALPHA_OFFSET] != ALPHA_OPAQUE) {
			throw std::runtime_error(std::format(
				"convertToPalette: Pixel {} has color 0x{:08X} not found in palette.",
				i,
				packRgbaKey(image[offset], image[offset + 1], image[offset + 2], image[offset + ALPHA_OFFSET])));
		}

		const std::uint32_t key = packRgbKey(image[offset], image[offset + 1], image[offset + 2]);
		const std::uint16_t palette_index = color_to_index[static_cast<std::size_t>(key)];
		if (palette_index == PALETTE_INDEX_MISSING) {
			throw std::runtime_error(std::format(
				"convertToPalette: Pixel {} has color 0x{:08X} not found in palette.",
				i,
				packRgbaKey(image[offset], image[offset + 1], image[offset + 2], ALPHA_OPAQUE)));
		}
		indexed_image[i] = static_cast<Byte>(palette_index);
	}
}

void mapPixelsToPaletteSorted(
	vBytes& indexed_image,
	const vBytes& image,
	const LodePNGColorStats& stats,
	std::size_t palette_size,
	std::size_t channels,
	bool has_alpha) {

	constexpr Byte ALPHA_OPAQUE = 255;
	const auto color_to_index = buildSortedPaletteIndex(stats, palette_size);

	for (std::size_t i = 0; i < indexed_image.size(); ++i) {
		const std::size_t offset = i * channels;
		const std::uint32_t key = packRgbaKey(
			image[offset],
			image[offset + 1],
			image[offset + 2],
			has_alpha ? image[offset + 3] : ALPHA_OPAQUE
		);

		const std::uint16_t palette_index = lookupSortedPaletteIndex(color_to_index, key);
		if (palette_index == PALETTE_INDEX_MISSING) {
			throw std::runtime_error(std::format(
				"convertToPalette: Pixel {} has color 0x{:08X} not found in palette.",
				i, key));
		}
		indexed_image[i] = static_cast<Byte>(palette_index);
	}
}

void convertToPalette(
	vBytes& image_file_vec,
	const vBytes& image,
	unsigned width,
	unsigned height,
	const LodePNGColorStats& stats,
	LodePNGColorType raw_color_type) {

	constexpr Byte PALETTE_BIT_DEPTH = 8;

	constexpr std::size_t
		RGBA_COMPONENTS  = 4,
		RGB_COMPONENTS   = 3,
		MAX_PALETTE_SIZE = 256,
		RGB_DIRECT_LOOKUP_MIN_PIXELS = 1ULL << 20;

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
	if (pixel_count >= RGB_DIRECT_LOOKUP_MIN_PIXELS && paletteIsOpaque(stats, palette_size)) {
		mapRgbPixelsToPaletteDirect(indexed_image, image, stats, palette_size, channels, has_alpha);
	} else {
		mapPixelsToPaletteSorted(indexed_image, image, stats, palette_size, channels, has_alpha);
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
		unsigned error = lodepng_palette_add(&encode_state.info_png.color, p[0], p[1], p[2], p[3]);
		if (error) {
			throw std::runtime_error(std::format("LodePNG palette error {}: {}", error, lodepng_error_text(error)));
		}
		error = lodepng_palette_add(&encode_state.info_raw, p[0], p[1], p[2], p[3]);
		if (error) {
			throw std::runtime_error(std::format("LodePNG palette error {}: {}", error, lodepng_error_text(error)));
		}
	}

	vBytes output;
	unsigned error = lodepng::encode(output, indexed_image.data(), width, height, encode_state);
	if (error) {
		throw std::runtime_error(std::format("LodePNG encode error: {}", error));
	}
	image_file_vec = std::move(output);
}

void stripAndCopyChunks(vBytes& image_file_vec, Byte color_type) {
	constexpr std::size_t MIN_PNG_SIZE = PNG_HEADER_SIZE + CHUNK_OVERHEAD + IHDR_DATA_SIZE + CHUNK_OVERHEAD;

	if (image_file_vec.size() < MIN_PNG_SIZE) {
		throw std::runtime_error("PNG Error: File too small to contain valid PNG structure.");
	}
	const PngChunkView ihdr = readRequiredIhdr(image_file_vec);
	compactChunksAfterIhdr(image_file_vec, ihdr.offset + ihdr.total_size, [color_type](const PngChunkView& chunk) {
		return
			(chunk.type == TYPE_PLTE && color_type == INDEXED_PLTE) ||
			chunk.type == TYPE_TRNS ||
			(chunk.type == TYPE_IDAT && !looksLikePdvrdtIdat(chunk.data)) ||
			chunk.type == TYPE_IEND;
	});
}

[[nodiscard]] DecodedImageForOptimization decodeImageForOptimization(const vBytes& image_file_vec) {
	lodepng::State state;
	lodepng_zlib_adapter::configureDecoder(state);

	DecodedImageForOptimization decoded;
	unsigned error = lodepng::decode(decoded.image, decoded.width, decoded.height, state, image_file_vec);
	if (error) {
		throw std::runtime_error(std::format("LodePNG decode error {}: {}", error, lodepng_error_text(error)));
	}

	lodepng_color_stats_init(&decoded.stats);
	decoded.stats.allow_palette   = 1;
	decoded.stats.allow_greyscale = 0;
	error = lodepng_compute_color_stats(&decoded.stats, decoded.image.data(), decoded.width, decoded.height, &state.info_raw);
	if (error) {
		throw std::runtime_error(std::format("LodePNG stats error: {}", error));
	}

	decoded.png_color_type = static_cast<Byte>(state.info_png.color.colortype);
	decoded.raw_color_type = state.info_raw.colortype;
	return decoded;
}

[[nodiscard]] bool hasUnsupportedShareDimensions(unsigned width, unsigned height, uint16_t max_dim) {
	constexpr uint16_t MIN_DIMS = 68;
	return width < MIN_DIMS || height < MIN_DIMS || width > max_dim || height > max_dim;
}

} // namespace

bool optimizeImage(vBytes& image_file_vec) {
	constexpr uint16_t
		MAX_PLTE_DIMS  = 4096,
		MAX_RGB_DIMS   = 900,
		MIN_RGB_COLORS = 257;

	preflightPngDecode(image_file_vec);
	removeExistingPdvrdtIdatChunks(image_file_vec);

	const DecodedImageForOptimization decoded = decodeImageForOptimization(image_file_vec);
	const Byte color_type = decoded.png_color_type;
	const bool is_truecolor = (color_type == TRUECOLOR_RGB || color_type == TRUECOLOR_RGBA);
	const bool can_palettize = is_truecolor && (decoded.stats.numcolors < MIN_RGB_COLORS);

	if (can_palettize) {
		convertToPalette(
			image_file_vec,
			decoded.image,
			decoded.width,
			decoded.height,
			decoded.stats,
			decoded.raw_color_type
		);
		return hasUnsupportedShareDimensions(decoded.width, decoded.height, MAX_PLTE_DIMS);
	}

	const uint16_t max_dim = (color_type == INDEXED_PLTE) ? MAX_PLTE_DIMS : MAX_RGB_DIMS;
	stripAndCopyChunks(image_file_vec, color_type);
	return hasUnsupportedShareDimensions(decoded.width, decoded.height, max_dim);
}
