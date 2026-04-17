#include "png_utils.h"
#include "io_utils.h"

#include <bit>
#include <cstring>
#include <format>
#include <stdexcept>

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
		case 2: write(static_cast<uint16_t>(value)); break;
		case 4: write(static_cast<uint32_t>(value)); break;
		case 8: write(static_cast<uint64_t>(value)); break;
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
		case 8: return read.operator()<uint64_t>();
		default:
			throw std::invalid_argument(std::format("getValue: unsupported length {}", length));
	}
}
