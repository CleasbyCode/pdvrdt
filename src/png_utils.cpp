#include "png_utils.h"
#include "io_utils.h"
#include "lodepng/lodepng_config.h"
#include "lodepng/lodepng.h"

#include <bit>
#include <cstring>
#include <format>
#include <limits>
#include <stdexcept>

namespace {
constexpr auto PNG_SIG = std::to_array<Byte>({
	0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
});
constexpr std::size_t
	CHUNK_HEADER_SIZE = 8,
	CHUNK_CRC_SIZE    = 4;
} // namespace

void updateValue(std::span<Byte> data, std::size_t index, std::uint64_t value, std::size_t length) {

	if (!spanHasRange(data, index, length)) {
		throw std::out_of_range("updateValue: Index out of bounds.");
	}

	auto write = [&]<typename T>(T val) {
		if constexpr (std::endian::native == std::endian::little) {
			val = std::byteswap(val);
		}
		std::memcpy(data.data() + index, &val, sizeof(T));
	};

	switch (length) {
		case 2:
			if (value > std::numeric_limits<std::uint16_t>::max()) {
				throw std::out_of_range("updateValue: value does not fit in 2 bytes.");
			}
			write(static_cast<std::uint16_t>(value));
			break;
		case 4:
			if (value > std::numeric_limits<std::uint32_t>::max()) {
				throw std::out_of_range("updateValue: value does not fit in 4 bytes.");
			}
			write(static_cast<std::uint32_t>(value));
			break;
		case 8: write(static_cast<std::uint64_t>(value)); break;
		default:
			throw std::invalid_argument(std::format("updateValue: unsupported length {}", length));
	}
}

std::size_t getValue(std::span<const Byte> data, std::size_t index, std::size_t length) {

	if (!spanHasRange(data, index, length)) {
		throw std::out_of_range("getValue: index out of bounds");
	}

	auto read = [&]<typename T>() -> T {
		T val;
		std::memcpy(&val, data.data() + index, sizeof(T));
		if constexpr (std::endian::native == std::endian::little) {
			val = std::byteswap(val);
		}
		return val;
	};

	switch (length) {
		case 2: return read.operator()<uint16_t>();
		case 4: return read.operator()<uint32_t>();
		case 8: {
			const std::uint64_t value = read.operator()<uint64_t>();
			if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
				if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
					throw std::out_of_range("getValue: value does not fit in size_t");
				}
			}
			return static_cast<std::size_t>(value);
		}
		default:
			throw std::invalid_argument(std::format("getValue: unsupported length {}", length));
	}
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

	const std::size_t length = getValue(png, offset, 4);
	const std::size_t type_index = offset + 4;
	const std::size_t data_index = type_index + 4;

	if (length > png.size() - data_index ||
		CHUNK_CRC_SIZE > png.size() - (data_index + length)) {
		throw std::runtime_error(std::string(length_error));
	}

	const std::size_t crc_index = data_index + length;
	requireSpanRange(png, data_index, length, length_error);
	requireSpanRange(png, crc_index, CHUNK_CRC_SIZE, crc_error);

	const std::uint32_t stored_crc = static_cast<std::uint32_t>(getValue(png, crc_index, 4));
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
		.type = static_cast<std::uint32_t>(getValue(png, type_index, 4)),
		.data = std::span<const Byte>(
			png.data() + static_cast<std::ptrdiff_t>(data_index),
			length
		)
	};
}
