#include "image.h"
#include "lodepng/lodepng_zlib_adapter.h"
#include "png_utils.h"

namespace {

void convertToPalette(vBytes& image_file_vec, const vBytes& image, unsigned width, unsigned height,
		const LodePNGColorStats& stats, LodePNGColorType raw_color_type) {

	constexpr Byte
		PALETTE_BIT_DEPTH = 8,
		ALPHA_OPAQUE      = 255;

	constexpr std::size_t
		RGBA_COMPONENTS = 4,
		RGB_COMPONENTS  = 3;

	const std::size_t
		palette_size = stats.numcolors,
		channels     = (raw_color_type == LCT_RGBA) ? RGBA_COMPONENTS : RGB_COMPONENTS;

	vBytes palette(palette_size * RGBA_COMPONENTS);
	std::unordered_map<uint32_t, Byte> color_to_index;
	color_to_index.reserve(palette_size);

	for (std::size_t i = 0; i < palette_size; ++i) {
		const Byte* src = &stats.palette[i * RGBA_COMPONENTS];
		std::copy_n(src, RGBA_COMPONENTS, &palette[i * RGBA_COMPONENTS]);

		const uint32_t key = (src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3];
		color_to_index[key] = static_cast<Byte>(i);
	}

	vBytes indexed_image(width * height);
	for (std::size_t i = 0; i < width * height; ++i) {
		const Byte
			r = image[i * channels],
			g = image[i * channels + 1],
			b = image[i * channels + 2],
			a = (channels == RGBA_COMPONENTS) ? image[i * channels + 3] : ALPHA_OPAQUE;

		const uint32_t key = (r << 24) | (g << 16) | (b << 8) | a;
		indexed_image[i] = color_to_index.at(key);
	}

	lodepng::State encode_state;
	lodepng_zlib_adapter::configureEncoder(encode_state);
	encode_state.info_raw.colortype       = LCT_PALETTE;
	encode_state.info_raw.bitdepth        = PALETTE_BIT_DEPTH;
	encode_state.info_png.color.colortype = LCT_PALETTE;
	encode_state.info_png.color.bitdepth  = PALETTE_BIT_DEPTH;
	encode_state.encoder.auto_convert     = 0;

	for (std::size_t i = 0; i < palette_size; ++i) {
		const Byte* p = &palette[i * RGBA_COMPONENTS];
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

void stripAndCopyChunks(vBytes& image_file_vec, Byte color_type) {
	constexpr Byte INDEXED_PLTE = 3;

	constexpr auto
		PLTE_SIG = std::to_array<Byte>({ 0x50, 0x4C, 0x54, 0x45 }),
		TRNS_SIG = std::to_array<Byte>({ 0x74, 0x52, 0x4E, 0x53 }),
		IDAT_SIG = std::to_array<Byte>({ 0x49, 0x44, 0x41, 0x54 });
	constexpr auto
		IEND_SIG = std::to_array<Byte>({ 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 });

	// Pdvrdt signature found at the start of a steganographic IDAT payload.
	constexpr std::array<Byte, 3> PDVRDT_IDAT_SIG = { 0x78, 0x5E, 0x5C };

	constexpr std::size_t
		PNG_HEADER_AND_IHDR_SIZE = 33,
		PNG_IEND_SIZE            = 12,
		IEND_TOTAL_SIZE          = 8;  // name(4) + crc(4)

	// Truncate any trailing data after IEND.
	if (auto pos_opt = searchSig(image_file_vec, IEND_SIG)) {
		const std::size_t end_index = *pos_opt + IEND_TOTAL_SIZE;
		if (end_index <= image_file_vec.size()) {
			image_file_vec.resize(end_index);
		}
	}

	vBytes cleaned_png;
	cleaned_png.reserve(image_file_vec.size());

	// Copy PNG signature + IHDR chunk.
	cleaned_png.insert(cleaned_png.end(),
		image_file_vec.begin(),
		image_file_vec.begin() + PNG_HEADER_AND_IHDR_SIZE);

	auto copy_chunks_of_type = [&](std::span<const Byte> chunk_sig) {
		constexpr std::size_t
			CHUNK_OVERHEAD    = 12, // length(4) + name(4) + crc(4)
			LENGTH_FIELD_SIZE = 4;

		std::size_t search_pos = 0;

		while (auto chunk_opt = searchSig(image_file_vec, chunk_sig, search_pos)) {
			const std::size_t name_index = *chunk_opt;

			// Skip pdvrdt steganographic IDAT chunks.
			if (std::equal(
				PDVRDT_IDAT_SIG.begin(), PDVRDT_IDAT_SIG.end(),
				image_file_vec.begin() + name_index + 4)) {
				break;
			}

			const std::size_t
				chunk_start  = name_index - LENGTH_FIELD_SIZE,
				chunk_length = getValue<4>(image_file_vec, chunk_start) + CHUNK_OVERHEAD;

			cleaned_png.insert(cleaned_png.end(),
				image_file_vec.begin() + chunk_start,
				image_file_vec.begin() + chunk_start + chunk_length);

			search_pos = name_index + 5;
		}
	};

	if (color_type == INDEXED_PLTE) {
		copy_chunks_of_type(PLTE_SIG);
	}
	copy_chunks_of_type(TRNS_SIG);
	copy_chunks_of_type(IDAT_SIG);

	// Copy IEND chunk.
	cleaned_png.insert(cleaned_png.end(),
		image_file_vec.end() - PNG_IEND_SIZE,
		image_file_vec.end());

	image_file_vec = std::move(cleaned_png);
}

} // namespace

ImageCheckResult optimizeImage(vBytes& image_file_vec) {
	constexpr Byte
		TRUECOLOR_RGB  = 2,
		TRUECOLOR_RGBA = 6;

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
	unsigned width = 0, height = 0;
	unsigned error = lodepng::decode(image, width, height, state, image_file_vec);
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
		return { .has_bad_dims = check_dims(MAX_PLTE_DIMS) };
	}

	stripAndCopyChunks(image_file_vec, color_type);
	return { .has_bad_dims = check_dims(MAX_RGB_DIMS) };
}
