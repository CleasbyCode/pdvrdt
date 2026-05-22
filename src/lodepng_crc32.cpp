#include "lodepng/lodepng_config.h"
#include "lodepng/lodepng.h"

#include <algorithm>
#include <limits>
#include <zlib.h>

unsigned lodepng_crc32(const unsigned char* data, size_t length) {
	uLong crc = crc32(0L, Z_NULL, 0);
	std::size_t offset = 0;

	while (offset < length) {
		const std::size_t chunk_size = std::min<std::size_t>(
			length - offset,
			static_cast<std::size_t>(std::numeric_limits<uInt>::max())
		);
		crc = crc32(
			crc,
			data + static_cast<std::ptrdiff_t>(offset),
			static_cast<uInt>(chunk_size)
		);
		offset += chunk_size;
	}

	return static_cast<unsigned>(crc);
}
