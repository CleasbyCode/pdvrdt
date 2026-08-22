#pragma once

#include "common.h"

#include <span>
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

[[nodiscard]] PngChunkView readPngChunk(
	std::span<const Byte> png,
	std::size_t offset,
	std::string_view header_error,
	std::string_view length_error,
	std::string_view crc_error);
