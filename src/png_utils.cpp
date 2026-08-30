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

#include <bit>
#include <cstring>
#include <stdexcept>

namespace {
constexpr auto PNG_SIG = std::to_array<Byte>({
	0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
});
constexpr std::size_t
	CHUNK_HEADER_SIZE = 8,
	CHUNK_CRC_SIZE    = 4,
	CHUNK_OVERHEAD    = CHUNK_HEADER_SIZE + CHUNK_CRC_SIZE,
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
	for (std::size_t pass = 0; pass < X_START.size(); ++pass) {
		if (width <= X_START[pass] || height <= Y_START[pass]) {
			continue;  // Empty pass: no rows, so no filter bytes either.
		}
		const std::size_t pass_width  = (width  - X_START[pass] + X_STEP[pass] - 1) / X_STEP[pass];
		const std::size_t pass_height = (height - Y_START[pass] + Y_STEP[pass] - 1) / Y_STEP[pass];
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

	const std::size_t crc_index = data_index + length;
	requireSpanRange(png, data_index, length, length_error);
	requireSpanRange(png, crc_index, CHUNK_CRC_SIZE, crc_error);

	const std::uint32_t stored_crc = getValue(png, crc_index);
	const std::uint32_t computed_crc = static_cast<std::uint32_t>(lodepng_crc32(
		png.data() + static_cast<std::ptrdiff_t>(type_index),
		length + 4
	));
	if (stored_crc != computed_crc) {
		throw std::runtime_error(std::string(crc_error));
	}

	return PngChunkView{
		.offset = offset,
		.length = length,
		.total_size = length + CHUNK_HEADER_SIZE + CHUNK_CRC_SIZE,
		.type = getValue(png, type_index),
		.data = std::span<const Byte>(
			png.data() + static_cast<std::ptrdiff_t>(data_index),
			length
		)
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
	requireSpanRange(png, 0, PNG_HEADER_SIZE + CHUNK_OVERHEAD + IHDR_DATA_SIZE,
		"PNG Error: File too small to contain valid PNG structure.");
	const PngChunkView ihdr = readRequiredIhdr(png);

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
// How many bytes of the concatenated IDAT stream the real zlib stream occupies.
// nullopt when the data does not decode as a complete zlib stream at all -- that
// is a genuinely corrupt image, and the decoder should report it in its own
// words rather than have this silently truncate the picture away.
//
// The output is discarded: only the input cursor matters. preflightPngDecode()
// has already bounded the declared geometry, and the ceiling below is a second
// belt on the inflate itself.
[[nodiscard]] std::optional<std::size_t> zlibStreamConsumedBytes(std::span<const Byte> data) {
	if (data.empty() || data.size() > static_cast<std::size_t>(std::numeric_limits<uInt>::max())) {
		return std::nullopt;
	}

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
	stream.next_in = const_cast<Byte*>(data.data());
	stream.avail_in = static_cast<uInt>(data.size());

	while (true) {
		stream.next_out = scratch.data();
		stream.avail_out = static_cast<uInt>(scratch.size());
		const int ret = inflate(&stream, Z_NO_FLUSH);
		if (ret == Z_STREAM_END) {
			return static_cast<std::size_t>(stream.total_in);
		}
		if (ret != Z_OK && ret != Z_BUF_ERROR) {
			return std::nullopt;
		}
		if (stream.total_out > MAX_LODEPNG_DECODE_BYTES) {
			return std::nullopt;
		}
		if (ret == Z_BUF_ERROR && stream.avail_in == 0) {
			return std::nullopt;  // truncated stream, not a trailing-data one
		}
	}
}

// Drop image data that sits past the end of the PNG's own zlib stream.
//
// Some tools append a spare IDAT chunk after the real image data. The result is
// still a structurally valid PNG -- every chunk has a good CRC -- but the
// concatenated IDAT payload is one complete zlib stream followed by bytes that
// belong to nothing, which is what the decoder refuses. Since those bytes are
// provably not part of the picture (the stream already ended), they can simply
// be removed and the cover used as-is, rather than making the user go and
// re-save the file.
//
// Truncation is done at byte granularity, not chunk granularity: the boundary
// usually falls exactly at the end of a chunk, but nothing guarantees it, and
// keeping a partial chunk correct is just a length and a CRC.
//
// Returns true when something was removed.
} // namespace

bool stripTrailingIdatData(vBytes& image_file_vec) {
	requireSpanRange(image_file_vec, 0, PNG_HEADER_SIZE, "PNG Error: Invalid PNG signature.");
	const PngChunkView ihdr = readRequiredIhdr(image_file_vec);

	vBytes idat_data;
	forEachChunkToIend(image_file_vec, ihdr.offset + ihdr.total_size, [&](const PngChunkView& chunk) {
		if (chunk.type == TYPE_IDAT) {
			appendBytes(idat_data, chunk.data, "PNG Error: IDAT size overflow.");
		}
	});
	if (idat_data.empty()) {
		return false;
	}

	const std::optional<std::size_t> consumed = zlibStreamConsumedBytes(idat_data);
	if (!consumed || *consumed >= idat_data.size()) {
		return false;
	}

	// Rebuild, keeping only the first `*consumed` bytes of IDAT payload.
	std::size_t kept = 0;
	vBytes rebuilt;
	appendBytes(
		rebuilt,
		std::span<const Byte>(image_file_vec).first(ihdr.offset + ihdr.total_size),
		"PNG Error: Encoded image size overflow."
	);

	forEachChunkToIend(image_file_vec, ihdr.offset + ihdr.total_size, [&](const PngChunkView& chunk) {
		if (chunk.type != TYPE_IDAT) {
			appendBytes(
				rebuilt,
				std::span<const Byte>(image_file_vec).subspan(chunk.offset, chunk.total_size),
				"PNG Error: Encoded image size overflow."
			);
			return;
		}

		const std::size_t remaining = *consumed - kept;
		const std::size_t take = std::min(remaining, chunk.length);
		kept += take;
		if (take == 0) {
			return;  // wholly past the end of the stream
		}
		if (take == chunk.length) {
			appendBytes(
				rebuilt,
				std::span<const Byte>(image_file_vec).subspan(chunk.offset, chunk.total_size),
				"PNG Error: Encoded image size overflow."
			);
			return;
		}

		// Partial chunk: re-emit it with a corrected length and CRC.
		std::array<Byte, 8> header{};
		updateValue(header, 0, static_cast<std::uint32_t>(take));
		updateValue(header, 4, TYPE_IDAT);
		appendBytes(rebuilt, header, "PNG Error: Encoded image size overflow.");
		appendBytes(rebuilt, chunk.data.first(take), "PNG Error: Encoded image size overflow.");

		std::uint32_t crc = pdvrdtCrc32Update(0, std::span<const Byte>(header).subspan(4, 4));
		crc = pdvrdtCrc32Update(crc, chunk.data.first(take));
		std::array<Byte, 4> crc_bytes{};
		updateValue(crc_bytes, 0, crc);
		appendBytes(rebuilt, crc_bytes, "PNG Error: Encoded image size overflow.");
	});

	image_file_vec = std::move(rebuilt);
	return true;
}
