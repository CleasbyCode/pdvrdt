#include "png_utils.h"
#include "io_utils.h"
#include "lodepng/lodepng_config.h"
#include "lodepng/lodepng.h"
#include "lodepng/lodepng_zlib_adapter.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <ranges>

#include <bit>
#include <cstring>
#include <stdexcept>

namespace {
constexpr auto PNG_SIG = std::to_array<Byte>({
	0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
});
constexpr std::size_t
	CHUNK_HEADER_SIZE = 8,
	CHUNK_CRC_SIZE    = PNG_CHUNK_OVERHEAD - CHUNK_HEADER_SIZE,
	// Same ceiling the zlib adapter enforces while inflating, so an oversized
	// image is rejected here with a clear message instead of failing opaquely
	// inside lodepng. Defined once, in the adapter.
	MAX_LODEPNG_DECODE_BYTES = lodepng_zlib_adapter::MAX_DECOMPRESS_SIZE;

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

// Bytes lodepng inflates the IDAT stream into for a single (sub-)image: one
// filter byte plus a byte-padded row of samples, per row.
[[nodiscard]] std::size_t filteredImageBytes(
	std::size_t width, std::size_t height, std::size_t bits_per_pixel) {

	constexpr const char* OVERFLOW_ERROR = "PNG Error: Inflated image size overflow.";
	if (width == 0 || height == 0) {
		return 0;
	}
	const std::size_t row_bits  = checkedMulSize(width, bits_per_pixel, OVERFLOW_ERROR);
	const std::size_t row_bytes = checkedAddSize(row_bits / 8, (row_bits % 8) ? 1 : 0, OVERFLOW_ERROR);
	return checkedMulSize(
		checkedAddSize(row_bytes, 1, OVERFLOW_ERROR),
		height,
		OVERFLOW_ERROR
	);
}

// Total inflated size, honouring the interlace method. Adam7 splits the image
// into seven sub-images, each with its own byte-padded rows and its own filter
// byte per row, which comes to *more* than the non-interlaced layout (~1.875x
// the filter bytes, plus per-pass row padding). Using the non-interlaced formula
// for an interlaced cover would therefore under-count and let the safety limit
// be exceeded, so each pass is measured.
[[nodiscard]] std::size_t inflatedImageBytes(
	std::size_t width, std::size_t height, std::size_t bits_per_pixel, Byte interlace_method) {

	constexpr const char* OVERFLOW_ERROR = "PNG Error: Inflated image size overflow.";
	if (interlace_method == 0) {
		return filteredImageBytes(width, height, bits_per_pixel);
	}

	// Adam7 pass origins and strides (PNG spec, 4.7 "Interlaced data order").
	constexpr std::array<std::size_t, 7>
		X_START { 0, 4, 0, 2, 0, 1, 0 },
		Y_START { 0, 0, 4, 0, 2, 0, 1 },
		X_STEP  { 8, 8, 4, 4, 2, 2, 1 },
		Y_STEP  { 8, 8, 8, 4, 4, 2, 2 };

	std::size_t total = 0;
	for (const auto [x_start, y_start, x_step, y_step] :
			std::views::zip(X_START, Y_START, X_STEP, Y_STEP)) {
		if (width <= x_start || height <= y_start) {
			continue;  // Empty pass: no rows, so no filter bytes either.
		}
		const std::size_t pass_width  = (width  - x_start + x_step - 1) / x_step;
		const std::size_t pass_height = (height - y_start + y_step - 1) / y_step;
		total = checkedAddSize(
			total,
			filteredImageBytes(pass_width, pass_height, bits_per_pixel),
			OVERFLOW_ERROR
		);
	}
	return total;
}

} // namespace

void updateValue(std::span<Byte> data, std::size_t index, std::uint32_t value) {
	if (!spanHasRange(data, index, sizeof(value))) {
		throw std::out_of_range("updateValue: Index out of bounds.");
	}
	if constexpr (std::endian::native == std::endian::little) {
		value = std::byteswap(value);
	}
	std::memcpy(data.data() + index, &value, sizeof(value));
}

std::uint32_t getValue(std::span<const Byte> data, std::size_t index) {
	if (!spanHasRange(data, index, sizeof(std::uint32_t))) {
		throw std::out_of_range("getValue: index out of bounds");
	}
	std::uint32_t value;
	std::memcpy(&value, data.data() + index, sizeof(value));
	if constexpr (std::endian::native == std::endian::little) {
		value = std::byteswap(value);
	}
	return value;
}

bool hasPngSignature(std::span<const Byte> data) {
	return spanHasRange(data, 0, PNG_SIG.size()) &&
		std::memcmp(data.data(), PNG_SIG.data(), PNG_SIG.size()) == 0;
}

void requirePngSignature(std::span<const Byte> data, std::string_view message) {
	if (!hasPngSignature(data)) {
		throw std::runtime_error(std::string(message));
	}
}

PngChunkView readPngChunk(
	std::span<const Byte> png,
	std::size_t offset,
	std::string_view header_error,
	std::string_view length_error,
	std::string_view crc_error) {

	requireSpanRange(png, offset, CHUNK_HEADER_SIZE, header_error);

	const std::size_t length = getValue(png, offset);
	const std::size_t type_index = offset + 4;
	const std::size_t data_index = type_index + 4;

	if (length > png.size() - data_index ||
		CHUNK_CRC_SIZE > png.size() - (data_index + length)) {
		throw std::runtime_error(std::string(length_error));
	}

	// The check above proved [data_index, crc_index + CHUNK_CRC_SIZE) in range.
	const std::size_t crc_index = data_index + length;
	if (getValue(png, crc_index) != pdvrdtCrc32Update(0, png.subspan(type_index, length + 4))) {
		throw std::runtime_error(std::string(crc_error));
	}

	return PngChunkView{
		.offset = offset,
		.length = length,
		.total_size = length + PNG_CHUNK_OVERHEAD,
		.type = getValue(png, type_index),
		.data = png.subspan(data_index, length)
	};
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

void requirePngGeometryWithinDecodeLimit(std::span<const Byte> png) {
	requireSpanRange(png, 0, PNG_HEADER_SIZE + PNG_CHUNK_OVERHEAD + IHDR_DATA_SIZE,
		"PNG Error: File too small to contain valid PNG structure.");
	requirePngGeometryWithinDecodeLimit(readRequiredIhdr(png));
}

void requirePngGeometryWithinDecodeLimit(const PngChunkView& ihdr) {
	const std::uint32_t width = getValue(ihdr.data, 0);
	const std::uint32_t height = getValue(ihdr.data, 4);
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
	const std::size_t inflated_size = inflatedImageBytes(
		static_cast<std::size_t>(width),
		static_cast<std::size_t>(height),
		bits_per_pixel,
		interlace_method
	);
	if (inflated_size > MAX_LODEPNG_DECODE_BYTES) {
		throw std::runtime_error("PNG Error: Inflated image exceeds safety limit.");
	}
}

namespace {
// How many bytes of the concatenated IDAT payload the image's own zlib stream
// occupies, fed to inflate chunk by chunk rather than gathered into a copy.
// nullopt when the data does not decode as a complete zlib stream at all --
// that is a genuinely corrupt image, and the decoder should report it in its
// own words rather than have this silently truncate the picture away.
//
// The output is discarded: only the input cursor matters. The geometry
// preflight has already bounded the declared size, and the ceiling below is a
// second belt on the inflate itself.
[[nodiscard]] std::optional<std::size_t> zlibStreamConsumedBytes(
	std::span<const Byte> png, std::size_t first_chunk_pos) {

	z_stream stream{};
	if (inflateInit(&stream) != Z_OK) {
		return std::nullopt;
	}
	struct InflateGuard {
		z_stream* s;
		~InflateGuard() { inflateEnd(s); }
	} guard{&stream};

	constexpr std::size_t SCRATCH_SIZE = 256 * 1024;
	vBytes scratch(SCRATCH_SIZE);
	std::optional<std::size_t> consumed;
	bool failed = false;

	forEachChunkToIend(png, first_chunk_pos, [&](const PngChunkView& chunk) {
		if (consumed || failed || chunk.type != TYPE_IDAT) {
			return;
		}
		// A chunk length is a 32-bit field, so it always fits zlib's uInt.
		stream.next_in = const_cast<Byte*>(chunk.data.data());
		stream.avail_in = static_cast<uInt>(chunk.data.size());
		while (stream.avail_in != 0) {
			stream.next_out = scratch.data();
			stream.avail_out = static_cast<uInt>(scratch.size());
			const int ret = inflate(&stream, Z_NO_FLUSH);
			if (ret == Z_STREAM_END) {
				consumed = static_cast<std::size_t>(stream.total_in);
				return;
			}
			// With a whole scratch buffer of room and input left, Z_BUF_ERROR
			// means a stalled stream, not a request for more input.
			if (ret != Z_OK || stream.total_out > MAX_LODEPNG_DECODE_BYTES) {
				failed = true;
				return;
			}
		}
	});
	// No Z_STREAM_END at all: a truncated stream, not a trailing-data one.
	return failed ? std::nullopt : consumed;
}
} // namespace

// Drop image data that sits past the end of the PNG's own zlib stream.
//
// Truncation is done at byte granularity, not chunk granularity: the boundary
// usually falls exactly at the end of a chunk, but nothing guarantees it, and
// keeping a partial chunk correct is just a length and a CRC. The result is
// never longer than the input, so it is compacted in place: every write lands
// at or behind the chunk being read, as in image.cpp's compactChunksAfterIhdr().
bool stripTrailingIdatData(vBytes& image_file_vec) {
	requireSpanRange(image_file_vec, 0, PNG_HEADER_SIZE, "PNG Error: Invalid PNG signature.");
	const PngChunkView ihdr = readRequiredIhdr(image_file_vec);
	const std::size_t first_chunk_pos = ihdr.offset + ihdr.total_size;

	std::size_t idat_total = 0;
	forEachChunkToIend(image_file_vec, first_chunk_pos, [&](const PngChunkView& chunk) {
		if (chunk.type == TYPE_IDAT) {
			idat_total = checkedAddSize(idat_total, chunk.length, "PNG Error: IDAT size overflow.");
		}
	});
	if (idat_total == 0) {
		return false;
	}
	const std::optional<std::size_t> consumed = zlibStreamConsumedBytes(image_file_vec, first_chunk_pos);
	if (!consumed || *consumed >= idat_total) {
		return false;
	}

	std::size_t kept = 0;
	std::size_t write_pos = first_chunk_pos;
	Byte* const base = image_file_vec.data();
	forEachChunkToIend(image_file_vec, first_chunk_pos, [&](const PngChunkView& chunk) {
		std::size_t take = chunk.total_size;  // bytes of this chunk kept, whole
		if (chunk.type == TYPE_IDAT) {
			const std::size_t data_kept = std::min(*consumed - kept, chunk.length);
			kept += data_kept;
			if (data_kept == 0) {
				return;  // wholly past the end of the stream
			}
			if (data_kept != chunk.length) {
				// Partial chunk: shift its surviving data down, then rewrite its
				// length and CRC. memmove first: the new header may overlap the
				// old one, but never the data being moved.
				std::memmove(base + write_pos + 8, chunk.data.data(), data_kept);
				updateValue(image_file_vec, write_pos, static_cast<std::uint32_t>(data_kept));
				updateValue(image_file_vec, write_pos + 4, TYPE_IDAT);
				updateValue(image_file_vec, write_pos + 8 + data_kept, pdvrdtCrc32Update(0,
					std::span<const Byte>(image_file_vec).subspan(write_pos + 4, 4 + data_kept)));
				write_pos += data_kept + PNG_CHUNK_OVERHEAD;
				return;
			}
		}
		if (write_pos != chunk.offset) {
			std::memmove(base + write_pos, base + chunk.offset, take);
		}
		write_pos += take;
	});

	image_file_vec.resize(write_pos);
	return true;
}
