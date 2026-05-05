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

// Write an integer as big-endian bytes into `vec` at the given byte offset.
// Length must be 2, 4, or 8.
void updateValue(std::span<Byte> data, std::size_t index, std::uint64_t value, std::size_t length = 4);

// Read a big-endian integer from `data` at the given byte offset.
// Length must be 2, 4, or 8.
[[nodiscard]] std::size_t getValue(std::span<const Byte> data, std::size_t index, std::size_t length = 4);

[[nodiscard]] bool hasPngSignature(std::span<const Byte> data);
void requirePngSignature(std::span<const Byte> data, std::string_view message);

[[nodiscard]] PngChunkView readPngChunk(
	std::span<const Byte> png,
	std::size_t offset,
	std::string_view header_error,
	std::string_view length_error,
	std::string_view crc_error);
