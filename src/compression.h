#pragma once

#include "common.h"

#include <functional>
#include <span>

using DeflateChunkHandler = std::function<void(std::span<const Byte>)>;

void zlibDeflate(vBytes& data_vec, Option option, bool is_compressed_file = false);
void zlibDeflateSpan(std::span<const Byte> data, Option option, bool is_compressed_file, const DeflateChunkHandler& on_chunk);
void zlibDeflateFile(const fs::path& path, Option option, bool is_compressed_file, const DeflateChunkHandler& on_chunk);
void zlibInflate(vBytes& data_vec);
[[nodiscard]] vBytes zlibInflateSpanBounded(std::span<const Byte> data, std::size_t max_output_size);
[[nodiscard]] std::size_t zlibInflateToFd(const vBytes& data_vec, int fd);
