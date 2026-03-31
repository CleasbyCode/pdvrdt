#pragma once

#include "common.h"

#include <span>

// Write an integer as big-endian bytes into `vec` at the given byte offset.
// Length must be 2, 4, or 8.
void updateValue(std::span<Byte> data, std::size_t index, std::uint64_t value, std::size_t length = 4);

// Read a big-endian integer from `data` at the given byte offset.
// Length must be 2, 4, or 8.
[[nodiscard]] std::size_t getValue(std::span<const Byte> data, std::size_t index, std::size_t length = 4);
