#pragma once

#include "common.h"

#include <functional>
#include <span>

using DeflateChunkHandler = std::function<void(std::span<const Byte>)>;

void zlibDeflateSpan(std::span<const Byte> data, Option option, bool is_compressed_file, const DeflateChunkHandler& on_chunk);
void zlibDeflateFd(int fd, std::size_t expected_size, Option option, bool is_compressed_file, const DeflateChunkHandler& on_chunk);
[[nodiscard]] vBytes zlibInflatePrefix(std::span<const Byte> data, std::size_t prefix_size);
[[nodiscard]] vBytes zlibInflateSpanBounded(std::span<const Byte> data, std::size_t max_output_size);
[[nodiscard]] std::size_t zlibInflateToFd(const vBytes& data_vec, int fd);
