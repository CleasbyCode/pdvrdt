#pragma once

#include "common.h"

#include <functional>
#include <span>

using DeflateChunkHandler = std::function<void(std::span<const Byte>)>;

// Deflate the secret payload read from `fd`. Inputs that are already in a
// compressed container are stored rather than deflated -- see payloadLevels()
// in compression.cpp.
void zlibDeflateFd(int fd, std::size_t expected_size, bool is_compressed_file, const DeflateChunkHandler& on_chunk);

// Wrap `data` in a *stored* (level 0) RFC 1950 stream. Used for the Mastodon
// iCCP profile, whose contents are ciphertext: deflating it costs real time and
// yields slightly more output than storing it.
void zlibStoreSpan(std::span<const Byte> data, const DeflateChunkHandler& on_chunk);

[[nodiscard]] vBytes zlibInflatePrefix(std::span<const Byte> data, std::size_t prefix_size);
[[nodiscard]] vBytes zlibInflateSpanBounded(std::span<const Byte> data, std::size_t max_output_size);
[[nodiscard]] std::size_t zlibInflateToFd(const vBytes& data_vec, int fd);
