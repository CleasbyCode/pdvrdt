#include "common.h"
#include "png_utils.h"
#include "lodepng/lodepng_config.h"
#include "lodepng/lodepng.h"

#include <libdeflate.h>

// PNG chunk CRC-32 for both pdvrdt and lodepng (LODEPNG_NO_COMPILE_CRC).
//
// libdeflate's CRC-32 is the same reflected IEEE polynomial, with the same
// running-value convention as zlib's crc32(), and it is already linked for
// compression. It runtime-dispatches to PCLMULQDQ / VPCLMULQDQ folding across
// several lanes, measured 2.6-4.7x faster than the single-lane PCLMUL fold it
// replaces, with bit-identical output.
std::uint32_t pdvrdtCrc32Update(std::uint32_t crc, std::span<const Byte> data) noexcept {
	return libdeflate_crc32(crc, data.data(), data.size());
}

unsigned lodepng_crc32(const unsigned char* data, std::size_t length) {
	return libdeflate_crc32(0, data, length);
}
