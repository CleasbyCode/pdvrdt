#pragma once

#include "common.h"

#include <functional>
#include <span>

using DeflateChunkHandler = std::move_only_function<void(std::span<const Byte>) const>;

// Deflate the secret payload read from `fd`. Inputs that are already in a
// compressed container are stored rather than deflated -- see StoredZlibWriter
// in compression.cpp.
void zlibDeflateFd(int fd, std::size_t expected_size, bool is_compressed_file, const DeflateChunkHandler& on_chunk);

// Exact size of the stored stream zlibStoreSpan()/zlibDeflateFd() emit for
// `input_size` bytes of stored input.
[[nodiscard]] std::size_t zlibStoredStreamSize(std::size_t input_size);

// Upper bound on what zlibDeflateFd() emits for `input_size` bytes: exact when
// the input is stored, generous when it is deflated. Incompressible input does
// not stay within zlibStoredStreamSize() once deflated -- the streaming back
// ends fall back to stored blocks of ~16 KiB, not 64 KiB, and measure ~0.03%
// over it -- so a buffer reserved to that size would have to grow once more,
// copying the whole payload, at the very end.
[[nodiscard]] std::size_t zlibPayloadStreamBound(std::size_t input_size, bool is_compressed_file);

// Wrap `data` in a *stored* (level 0) RFC 1950 stream. Used for the Mastodon
// iCCP profile, whose contents are ciphertext: deflating it costs real time and
// yields slightly more output than storing it.
void zlibStoreSpan(std::span<const Byte> data, const DeflateChunkHandler& on_chunk);

[[nodiscard]] vBytes zlibInflatePrefix(std::span<const Byte> data, std::size_t prefix_size);
[[nodiscard]] vBytes zlibInflateSpanBounded(std::span<const Byte> data, std::size_t max_output_size);
[[nodiscard]] std::size_t zlibInflateToFd(const vBytes& data_vec, int fd);
