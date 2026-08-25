#include "image.h"
#include "compression.h"
#include "encryption.h"
#include "io_utils.h"
#include "lodepng/lodepng_config.h"
#include "lodepng/lodepng.h"
#include "lodepng/lodepng_zlib_adapter.h"
#include "png_utils.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <limits>
#include <optional>
#include <print>
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

// TYPE_IHDR/IDAT/ICCP/IEND and the PNG/IHDR sizes come from png_utils.h.
constexpr std::uint32_t
	TYPE_PLTE = 0x504C5445u,
	TYPE_TRNS = 0x74524E53u,
	TYPE_CHRM = 0x6348524Du,
	TYPE_GAMA = 0x67414D41u,
	TYPE_SRGB = 0x73524742u,
	TYPE_SBIT = 0x73424954u,
	TYPE_CICP = 0x63494350u,
	TYPE_MDCV = 0x6D444356u,
	TYPE_CLLI = 0x634C4C49u,
	TYPE_ACTL = 0x6163544Cu,
	TYPE_FCTL = 0x6663544Cu,
	TYPE_FDAT = 0x66644154u;

constexpr std::size_t CHUNK_OVERHEAD = 12;

[[nodiscard]] constexpr std::uint32_t packRgbaKey(Byte red, Byte green, Byte blue, Byte alpha) {
	return
		(static_cast<std::uint32_t>(red) << 24)   |
		(static_cast<std::uint32_t>(green) << 16) |
		(static_cast<std::uint32_t>(blue) << 8)   |
		 static_cast<std::uint32_t>(alpha);
}

struct DecodedImageForOptimization {
	vBytes image{};
	unsigned width{};
	unsigned height{};
	LodePNGColorStats stats{};
	Byte png_color_type{};
	Byte png_bit_depth{};
	LodePNGColorType raw_color_type{};
};

struct PreservedColorMetadata {
	vBytes chunks{};
};

[[nodiscard]] constexpr bool isApngChunk(std::uint32_t type) {
	return type == TYPE_ACTL || type == TYPE_FCTL || type == TYPE_FDAT;
}

[[nodiscard]] constexpr bool isColorMetadataChunk(std::uint32_t type) {
	return
		type == TYPE_CHRM ||
		type == TYPE_GAMA ||
		type == TYPE_ICCP ||
		type == TYPE_SRGB ||
		type == TYPE_SBIT ||
		type == TYPE_CICP ||
		type == TYPE_MDCV ||
		type == TYPE_CLLI;
}

// A previous pdvrdt Mastodon payload must be removed when its image is reused as
// a cover, while a genuine ICC profile must be preserved. findPdvrdtIccpPayload()
// is the shared decision the recover side uses, so the two can never disagree.
[[nodiscard]] bool looksLikePdvrdtIccp(std::span<const Byte> chunk_data) {
	return findPdvrdtIccpPayload(chunk_data).has_value();
}



[[nodiscard]] bool looksLikePdvrdtIdat(std::span<const Byte> chunk_data) {
	return bytesEqualAt(chunk_data, 0, PDVRDT_IDAT_PREFIX) &&
		hasPdvrdtProfileMarkers(chunk_data.subspan(PDVRDT_IDAT_PREFIX.size()), DEFAULT_OFFSETS);
}



void validateStaticPngChunks(std::span<const Byte> png, std::size_t first_chunk_pos) {
	bool has_iccp = false;
	bool has_srgb = false;

	forEachChunkToIend(png, first_chunk_pos, [&](const PngChunkView& chunk) {
		if (isApngChunk(chunk.type)) {
			throw std::runtime_error("PNG Error: APNG covers are not supported.");
		}
		if (chunk.type == TYPE_ICCP) {
			if (has_iccp || has_srgb) {
				throw std::runtime_error(
					"PNG Error: Invalid color metadata. Duplicate iCCP or conflicting sRGB chunk.");
			}
			has_iccp = true;
		} else if (chunk.type == TYPE_SRGB) {
			if (has_srgb || has_iccp) {
				throw std::runtime_error(
					"PNG Error: Invalid color metadata. Duplicate sRGB or conflicting iCCP chunk.");
			}
			has_srgb = true;
		}
	});
}

template <typename KeepChunk>
void compactChunksAfterIhdr(vBytes& image_file_vec, std::size_t first_chunk_pos, KeepChunk&& keep_chunk) {
	// Compaction only ever writes behind the read position, so the walk is
	// unaffected by it and the vector is truncated once at the end.
	std::size_t write_pos = first_chunk_pos;

	forEachChunkToIend(image_file_vec, first_chunk_pos, [&](const PngChunkView& chunk) {
		if (!keep_chunk(chunk)) {
			return;
		}
		if (write_pos != chunk.offset) {
			std::memmove(image_file_vec.data() + write_pos, image_file_vec.data() + chunk.offset, chunk.total_size);
		}
		write_pos += chunk.total_size;
	});

	image_file_vec.resize(write_pos);
}

} // namespace

void preflightPngDecode(std::span<const Byte> png) {
	// Two halves. The geometry bound lives in png_utils so the recover-only
	// binary can reach it without linking this file; the chunk policy below is
	// cover-image policy and stays here.
	requirePngGeometryWithinDecodeLimit(png);

	const PngChunkView ihdr = readRequiredIhdr(png);
	validateStaticPngChunks(png, ihdr.offset + ihdr.total_size);
}

namespace {

void appendPaletteSbitFromRgba(
	vBytes& chunks,
	std::span<const Byte> rgba_sbit) {

	constexpr std::size_t
		RGBA_SBIT_SIZE    = 4,
		PALETTE_SBIT_SIZE = 3,
		CHUNK_TOTAL_SIZE  = 4 + 4 + PALETTE_SBIT_SIZE + 4;

	if (rgba_sbit.size() != RGBA_SBIT_SIZE) {
		throw std::runtime_error("PNG Error: Invalid RGBA sBIT metadata.");
	}

	std::array<Byte, CHUNK_TOTAL_SIZE> chunk{};
	updateValue(chunk, 0, static_cast<std::uint32_t>(PALETTE_SBIT_SIZE));
	updateValue(chunk, 4, TYPE_SBIT);
	std::ranges::copy(rgba_sbit.first(PALETTE_SBIT_SIZE), chunk.begin() + 8);

	const std::span<const Byte> type_and_data(chunk.data() + 4, 4 + PALETTE_SBIT_SIZE);
	updateValue(chunk, 8 + PALETTE_SBIT_SIZE, pdvrdtCrc32Update(0, type_and_data));
	appendBytes(chunks, chunk, "PNG Error: Color metadata size overflow.");
}

[[nodiscard]] PreservedColorMetadata collectColorMetadata(
	std::span<const Byte> png,
	bool palette_from_rgba) {

	const PngChunkView ihdr = readRequiredIhdr(png);
	PreservedColorMetadata metadata;

	forEachChunkToIend(png, ihdr.offset + ihdr.total_size, [&](const PngChunkView& chunk) {
		if (isColorMetadataChunk(chunk.type) &&
			!(chunk.type == TYPE_ICCP && looksLikePdvrdtIccp(chunk.data))) {
			if (palette_from_rgba && chunk.type == TYPE_SBIT) {
				// Indexed PNG has no alpha field in sBIT. Preserve the RGB
				// significance fields and let tRNS carry the palette alpha values.
				appendPaletteSbitFromRgba(metadata.chunks, chunk.data);
			} else {
				appendBytes(
					metadata.chunks,
					png.subspan(chunk.offset, chunk.total_size),
					"PNG Error: Color metadata size overflow."
				);
			}
		}
	});
	return metadata;
}

void insertColorMetadataAfterIhdr(vBytes& png, std::span<const Byte> metadata_chunks) {
	if (metadata_chunks.empty()) {
		return;
	}

	const PngChunkView ihdr = readRequiredIhdr(png);
	const std::size_t insert_pos = ihdr.offset + ihdr.total_size;
	vBytes with_metadata;
	with_metadata.reserve(checkedAddSize(
		png.size(),
		metadata_chunks.size(),
		"PNG Error: Encoded image size overflow."
	));
	appendBytes(
		with_metadata,
		std::span<const Byte>(png).first(insert_pos),
		"PNG Error: Encoded image size overflow."
	);
	appendBytes(with_metadata, metadata_chunks, "PNG Error: Encoded image size overflow.");
	appendBytes(
		with_metadata,
		std::span<const Byte>(png).subspan(insert_pos),
		"PNG Error: Encoded image size overflow."
	);
	png = std::move(with_metadata);
}

void removeExistingPdvrdtIdatChunks(vBytes& image_file_vec) {
	requireSpanRange(image_file_vec, 0, PNG_HEADER_SIZE, "PNG Error: Invalid PNG signature.");
	const PngChunkView ihdr = readRequiredIhdr(image_file_vec);
	compactChunksAfterIhdr(image_file_vec, ihdr.offset + ihdr.total_size, [](const PngChunkView& chunk) {
		return chunk.type != TYPE_IDAT || !looksLikePdvrdtIdat(chunk.data);
	});
}

// Compact open-addressing hash from a 32-bit RGBA colour to its palette index.
// palette_size is capped at MAX_PALETTE_SIZE (256, validated in convertToPalette),
// so a 512-slot table stays at <= 50% load: ~1-2 probes per lookup, the whole
// table fits in L1, and there is no large allocation or zero-fill (unlike a
// 2^24-entry / 33 MiB direct LUT). Ported from pdvzip's PaletteIndexTable.
class PaletteIndexTable {
	static constexpr std::size_t TABLE_SIZE = 512;
	static constexpr std::size_t TABLE_MASK = TABLE_SIZE - 1;

	std::array<std::uint32_t, TABLE_SIZE> keys_{};
	std::array<Byte, TABLE_SIZE> values_{};
	std::array<bool, TABLE_SIZE> occupied_{};

	[[nodiscard]] static constexpr std::size_t hash(std::uint32_t key) {
		key ^= key >> 16;
		key *= 0x7feb352dU;
		key ^= key >> 15;
		key *= 0x846ca68bU;
		key ^= key >> 16;
		return static_cast<std::size_t>(key) & TABLE_MASK;
	}

public:
	void insertIfAbsent(std::uint32_t key, Byte value) {
		std::size_t slot = hash(key);
		for (std::size_t probe = 0; probe < TABLE_SIZE; ++probe) {
			if (!occupied_[slot]) {
				occupied_[slot] = true;
				keys_[slot] = key;
				values_[slot] = value;
				return;
			}
			if (keys_[slot] == key) {
				return;
			}
			slot = (slot + 1) & TABLE_MASK;
		}
		throw std::runtime_error("convertToPalette: Palette lookup table is full.");
	}

	[[nodiscard]] bool find(std::uint32_t key, Byte& value) const {
		std::size_t slot = hash(key);
		for (std::size_t probe = 0; probe < TABLE_SIZE; ++probe) {
			if (!occupied_[slot]) {
				return false;
			}
			if (keys_[slot] == key) {
				value = values_[slot];
				return true;
			}
			slot = (slot + 1) & TABLE_MASK;
		}
		return false;
	}
};

// Map every pixel to its palette index via the compact RGBA hash. Replaces the
// former direct-LUT (33 MiB) / sorted-binary-search split with a single path;
// keying on full RGBA also removes the opaque-only restriction the direct path
// required.
void mapPixelsToPalette(
	vBytes& indexed_image,
	const vBytes& image,
	const LodePNGColorStats& stats,
	std::size_t palette_size,
	std::size_t channels,
	bool has_alpha) {

	constexpr std::size_t RGBA_COMPONENTS = 4;
	constexpr Byte ALPHA_OPAQUE = 255;

	PaletteIndexTable color_to_index;
	for (std::size_t i = 0; i < palette_size; ++i) {
		const Byte* src = &stats.palette[i * RGBA_COMPONENTS];
		color_to_index.insertIfAbsent(packRgbaKey(src[0], src[1], src[2], src[3]), static_cast<Byte>(i));
	}

	for (std::size_t i = 0; i < indexed_image.size(); ++i) {
		const std::size_t offset = i * channels;
		const std::uint32_t key = packRgbaKey(
			image[offset],
			image[offset + 1],
			image[offset + 2],
			has_alpha ? image[offset + 3] : ALPHA_OPAQUE
		);

		Byte palette_index = 0;
		if (!color_to_index.find(key, palette_index)) {
			throw std::runtime_error(std::format(
				"convertToPalette: Pixel {} has color 0x{:08X} not found in palette.",
				i, key));
		}
		indexed_image[i] = palette_index;
	}
}

void convertToPalette(
	vBytes& image_file_vec,
	const vBytes& image,
	unsigned width,
	unsigned height,
	const LodePNGColorStats& stats,
	LodePNGColorType raw_color_type,
	std::span<const Byte> color_metadata) {

	constexpr Byte PALETTE_BIT_DEPTH = 8;

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
	mapPixelsToPalette(indexed_image, image, stats, palette_size, channels, has_alpha);

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
	insertColorMetadataAfterIhdr(output, color_metadata);
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
			(isColorMetadataChunk(chunk.type) &&
				!(chunk.type == TYPE_ICCP && looksLikePdvrdtIccp(chunk.data))) ||
			// Any pdvrdt IDAT payload is already gone: optimizeImage() runs
			// removeExistingPdvrdtIdatChunks() before this. Re-testing here would
			// be dead weight on every IDAT of every cover.
			chunk.type == TYPE_IDAT ||
			chunk.type == TYPE_IEND;
	});
}

// Decode without interpreting failures, so the caller can repair a cover whose
// IDAT stream has trailing bytes and try once more. Reports lodepng's own error
// code; 0 on success.
[[nodiscard]] unsigned tryDecodePng(
	const vBytes& image_file_vec,
	lodepng::State& state,
	DecodedImageForOptimization& decoded) {

	lodepng_zlib_adapter::configureDecoder(state);
	return lodepng::decode(decoded.image, decoded.width, decoded.height, state, image_file_vec);
}

[[nodiscard]] DecodedImageForOptimization decodeImageForOptimization(vBytes& image_file_vec) {
	lodepng::State state;
	DecodedImageForOptimization decoded;
	unsigned error = tryDecodePng(image_file_vec, state, decoded);

	// removeExistingPdvrdtIdatChunks() has already dropped any IDAT this tool
	// wrote, so leftover bytes mean some *other* tool appended one. Those bytes
	// sit past the end of the PNG's own zlib stream, so they are provably not
	// part of the picture: drop them and decode again rather than making the
	// user go and re-save the file.
	//
	// Done here, on the failure, rather than as a routine pre-pass: finding the
	// end of the stream costs a full inflate, and covers with a stray IDAT are
	// the rare case. A clean cover pays nothing for this.
	if (error == lodepng_zlib_adapter::ERROR_TRAILING_DATA && stripTrailingIdatData(image_file_vec)) {
		std::println("\nNote: removed a stray IDAT chunk left in the cover image by another tool.");
		state = lodepng::State{};
		decoded = DecodedImageForOptimization{};
		error = tryDecodePng(image_file_vec, state, decoded);
	}

	if (error == lodepng_zlib_adapter::ERROR_TRAILING_DATA) {
		// Still trailing data after the repair (or nothing could be repaired):
		// the IDAT stream is not a complete zlib stream followed by junk, so
		// this is a genuinely malformed image rather than an appended chunk.
		throw std::runtime_error(
			"PNG Error: The cover image's compressed image data does not end where the PNG "
			"says it should, and the leftover bytes are not a stray chunk that can be removed. "
			"Re-save the image with a PNG editor and try again.");
	}
	if (error) {
		throw std::runtime_error(std::format("LodePNG decode error {}: {}", error, lodepng_error_text(error)));
	}

	lodepng_color_stats_init(&decoded.stats);
	decoded.png_color_type = static_cast<Byte>(state.info_png.color.colortype);
	decoded.png_bit_depth  = static_cast<Byte>(state.info_png.color.bitdepth);
	decoded.raw_color_type = state.info_raw.colortype;

	const bool can_use_rgba8_stats =
		decoded.png_bit_depth == 8 &&
		(decoded.png_color_type == TRUECOLOR_RGB || decoded.png_color_type == TRUECOLOR_RGBA);
	if (can_use_rgba8_stats) {
		decoded.stats.allow_palette   = 1;
		decoded.stats.allow_greyscale = 0;
		error = lodepng_compute_color_stats(
			&decoded.stats,
			decoded.image.data(),
			decoded.width,
			decoded.height,
			&state.info_raw
		);
		if (error) {
			throw std::runtime_error(std::format("LodePNG stats error: {}", error));
		}
	}
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
	// Palette entries are 8-bit samples. Statistics from LodePNG's default RGBA8
	// decode cannot distinguish 16-bit values that share the same high byte, so
	// never use them to rewrite a 16-bit source.
	const bool can_palettize =
		is_truecolor &&
		decoded.png_bit_depth == 8 &&
		(decoded.stats.numcolors < MIN_RGB_COLORS);

	if (can_palettize) {
		const PreservedColorMetadata color_metadata = collectColorMetadata(
			image_file_vec,
			color_type == TRUECOLOR_RGBA
		);
		convertToPalette(
			image_file_vec,
			decoded.image,
			decoded.width,
			decoded.height,
			decoded.stats,
			decoded.raw_color_type,
			color_metadata.chunks
		);
		return hasUnsupportedShareDimensions(decoded.width, decoded.height, MAX_PLTE_DIMS);
	}

	const uint16_t max_dim = (color_type == INDEXED_PLTE) ? MAX_PLTE_DIMS : MAX_RGB_DIMS;
	stripAndCopyChunks(image_file_vec, color_type);
	return hasUnsupportedShareDimensions(decoded.width, decoded.height, max_dim);
}

void prepareImageForMastodonEmbedding(vBytes& image_file_vec) {
	const PngChunkView ihdr = readRequiredIhdr(image_file_vec);
	compactChunksAfterIhdr(
		image_file_vec,
		ihdr.offset + ihdr.total_size,
		[](const PngChunkView& chunk) {
			// Mastodon embeds the encrypted profile in iCCP. PNG permits only one
			// iCCP and forbids combining it with sRGB, so those cover declarations
			// are replaced while all compatible color metadata remains intact.
			return chunk.type != TYPE_ICCP && chunk.type != TYPE_SRGB;
		}
	);
}
