#pragma once

#include "common.h"
#include "io_utils.h"

#include <cstring>
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
	TYPE_TRNS = 0x74524E53u,
	TYPE_IEND = 0x49454E44u;

inline constexpr std::size_t
	PNG_HEADER_SIZE    = 8,
	PNG_CHUNK_OVERHEAD = 12,  // length + type + CRC
	IHDR_DATA_SIZE     = 13;

// Read/write a 32-bit big-endian field at a byte offset. Chunk lengths, chunk
// types, CRCs and the secretstream frame length are all 32-bit big-endian, and
// nothing in either format needs another width.
void updateValue(std::span<Byte> data, std::size_t index, std::uint32_t value);
[[nodiscard]] std::uint32_t getValue(std::span<const Byte> data, std::size_t index);

[[nodiscard]] bool hasPngSignature(std::span<const Byte> data);
void requirePngSignature(std::span<const Byte> data, std::string_view message);

// PNG chunk CRC-32 (IEEE, reflected), used to emit and verify chunk CRCs.
// Defined in lodepng_crc32.cpp on top of libdeflate_crc32().
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
//
// Returns the size the IDAT stream inflates to (the filtered scanlines).
std::size_t requirePngGeometryWithinDecodeLimit(std::span<const Byte> png);
// As above, for an IHDR the caller has already read with readRequiredIhdr().
std::size_t requirePngGeometryWithinDecodeLimit(const PngChunkView& ihdr);

// Reject a PNG that is out of all proportion to the `inflated_size` its
// geometry declares (see above), after first stripping any trailing junk past
// the end of its zlib stream. Call after retainPixelDecodeChunks(): the whole
// remaining file is measured, chunk headers included, because that is what
// lodepng sizes its private image-data buffer from while the input stays
// resident. Counting only IDAT payload would let empty or one-byte IDAT
// chunks, or a large unknown critical chunk (kept so the decoder can refuse
// it), through at any size -- independently of the pixel limit the geometry
// check enforces.
void requireDecodeInputWithinBound(vBytes& png, std::size_t inflated_size);

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

// Keep only the chunks `keep_chunk` accepts, compacting `png` in place and
// dropping anything after IEND. Compaction only ever writes behind the read
// position, so the walk is unaffected by it and the vector is truncated once
// at the end.
template <typename KeepChunk>
void compactChunksAfterIhdr(vBytes& png, std::size_t first_chunk_pos, KeepChunk&& keep_chunk) {
	std::size_t write_pos = first_chunk_pos;

	forEachChunkToIend(png, first_chunk_pos, [&](const PngChunkView& chunk) {
		if (!keep_chunk(chunk)) {
			return;
		}
		if (write_pos != chunk.offset) {
			std::memmove(png.data() + write_pos, png.data() + chunk.offset, chunk.total_size);
		}
		write_pos += chunk.total_size;
	});

	png.resize(write_pos);
}

// Reduce `png`, in place, to the chunks pixel decoding uses: every critical
// chunk (so an unknown critical chunk is still refused by the decoder) plus
// tRNS. lodepng is built without ancillary-chunk support and skips the rest
// anyway, but first it allocates a buffer the size of its whole input to
// gather the image data into -- so a tiny image carrying a 64 MiB tEXt chunk
// would cost a second 64 MiB before the decoder ever looked at a pixel.
void retainPixelDecodeChunks(vBytes& png);
