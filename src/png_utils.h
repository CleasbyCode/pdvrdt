#pragma once

#include "common.h"
#include "io_utils.h"

#include <span>
#include <stdexcept>
#include <string_view>

struct PngChunkView {
	std::size_t offset{};
	std::size_t length{};
	std::size_t total_size{};
	std::uint32_t type{};
	std::span<const Byte> data{};
};

// Structural constants shared by every walker. Chunk types are the big-endian
// 32-bit values readPngChunk() reports in PngChunkView::type.
inline constexpr std::uint32_t
	TYPE_IHDR = 0x49484452u,
	TYPE_IDAT = 0x49444154u,
	TYPE_ICCP = 0x69434350u,
	TYPE_IEND = 0x49454E44u;

inline constexpr std::size_t
	PNG_HEADER_SIZE = 8,
	IHDR_DATA_SIZE  = 13;

// Read/write a 32-bit big-endian field at a byte offset. Chunk lengths, chunk
// types, CRCs and the secretstream frame length are all 32-bit big-endian, and
// nothing in either format needs another width.
void updateValue(std::span<Byte> data, std::size_t index, std::uint32_t value);
[[nodiscard]] std::uint32_t getValue(std::span<const Byte> data, std::size_t index);

[[nodiscard]] bool hasPngSignature(std::span<const Byte> data);
void requirePngSignature(std::span<const Byte> data, std::string_view message);

// PNG chunk CRC-32 (IEEE, reflected), used to emit and verify chunk CRCs.
// Defined in lodepng_crc32.cpp, which runtime-dispatches to a PCLMULQDQ folding
// implementation on capable x86 CPUs and a slicing-by-8 scalar path elsewhere.
[[nodiscard]] std::uint32_t pdvrdtCrc32Update(
	std::uint32_t crc,
	std::span<const Byte> data) noexcept;


// Remove image data sitting past the end of the PNG's own zlib stream.
//
// Some tools append a spare IDAT chunk after the real image data. The result is
// still a structurally valid PNG -- every chunk has a good CRC -- but the
// concatenated IDAT payload is one complete zlib stream followed by bytes that
// belong to nothing, which is what the decoder refuses. Those bytes are provably
// not part of the picture (the stream already ended), so they can be dropped and
// the image used as-is. Returns true when something was removed.
[[nodiscard]] bool stripTrailingIdatData(vBytes& png);

// The IHDR chunk, validated as present, correctly sized and CRC-clean.
[[nodiscard]] PngChunkView readRequiredIhdr(std::span<const Byte> png);

// Reject a PNG whose declared IHDR geometry would inflate past the decoder's
// safety ceiling, before lodepng allocates anything for it.
//
// This is the half of the decode preflight that bounds *work*, kept here rather
// than beside the cover-image policy checks in image.cpp because both binaries
// need it: conceal reaches it through preflightPngDecode(), and recover through
// extractRedditPngPayload(), which is the one recovery path that decodes pixels
// and the one place an attacker-supplied PNG reaches the decoder unvetted.
void requirePngGeometryWithinDecodeLimit(std::span<const Byte> png);

[[nodiscard]] PngChunkView readPngChunk(
	std::span<const Byte> png,
	std::size_t offset,
	std::string_view header_error,
	std::string_view length_error,
	std::string_view crc_error);

// Walk every chunk from `first_chunk_pos` through IEND, handing each to `visit`.
// Owns the structural rules the walkers below all depend on -- valid header,
// length and CRC, a zero-length IEND, and an IEND that is actually present -- so
// they cannot drift apart. `visit` runs before the chunk is stepped over, and may
// rewrite bytes at or behind the current position (see compactChunksAfterIhdr).
template <typename Visit>
void forEachChunkToIend(std::span<const Byte> png, std::size_t first_chunk_pos, Visit&& visit) {
	std::size_t pos = first_chunk_pos;
	while (pos < png.size()) {
		const PngChunkView chunk = readPngChunk(
			png,
			pos,
			"PNG Error: Corrupt PNG chunk header.",
			"PNG Error: Corrupt PNG chunk length.",
			"PNG Error: Corrupt PNG chunk CRC."
		);
		if (chunk.type == TYPE_IEND && chunk.length != 0) {
			throw std::runtime_error("PNG Error: Corrupt PNG structure. Invalid IEND.");
		}
		visit(chunk);
		pos += chunk.total_size;
		if (chunk.type == TYPE_IEND) {
			return;
		}
	}
	throw std::runtime_error("PNG Error: Missing IEND chunk.");
}
