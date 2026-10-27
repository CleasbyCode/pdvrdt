// Adapter that bridges lodepng's custom_zlib callbacks to libdeflate, for
// both decompression and compression. Used when LODEPNG_NO_COMPILE_ZLIB is defined.

#pragma once

#include "lodepng_config.h"
#include "lodepng.h"
#include <cstdlib>
#include <libdeflate.h>

namespace lodepng_zlib_adapter {

// Ceiling on the inflated size of any zlib stream inside a cover PNG, i.e. the
// decompression-bomb guard.
//
// Sized to clear the largest cover the conceal modes accept. The binding case is
// the Reddit carrier's 8192x8192 limit at 8-bit RGBA, whose filtered scanlines
// come to 8192 * (1 + 8192*4) = 268,443,648 bytes -- 8 KiB past a flat 256 MiB,
// which is why that is not the number here. 272 MiB clears it with a little room
// and stops there: deeper 8192-square covers (16-bit colour, at 384 MiB and up)
// stay rejected, as they were before.
//
// This is the single definition: image.cpp's preflight rejects covers whose
// declared IHDR geometry would exceed it, so that the check the user sees is a
// clear error rather than an opaque decode failure here. Both must be the same
// number for that to hold.
inline constexpr size_t MAX_DECOMPRESS_SIZE = 272ULL * 1024 * 1024;

// Returned instead of the generic zlib failure (52) when the input decodes as a
// complete zlib stream but has bytes left over. lodepng passes custom_zlib codes
// through unchanged, and its own codes stop well below 1000, so the caller can
// recognise this one and explain the actual cause: a PNG whose concatenated IDAT
// data carries an extra chunk appended by another tool. Without it the failure
// surfaces as lodepng's unrelated text for code 52.
inline constexpr unsigned ERROR_TRAILING_DATA = 1052;

// LodePNG's existing error for exceeding a decompression output limit.
inline constexpr unsigned ERROR_OUTPUT_LIMIT = 109;

inline size_t decompressLimit(const LodePNGDecompressSettings* settings) {
	if (settings != nullptr && settings->max_output_size != 0 &&
		settings->max_output_size < MAX_DECOMPRESS_SIZE) {
		return settings->max_output_size;
	}
	return MAX_DECOMPRESS_SIZE;
}

// Decompress callback for lodepng (zlib format: header + deflate + adler32).
//
// The only live caller is lodepng's IDAT decode (the ancillary-chunk and
// generic zlib decoders are compiled out by lodepng_config.h), and it always
// supplies the exact filtered-scanline size through max_output_size. So one
// exactly sized libdeflate buffer always suffices, and needing more is an
// error, never a reason to grow it. Without a bound the global ceiling is the
// buffer size; malloc only commits the pages that are written.
inline unsigned decompress(
	unsigned char** out, size_t* outsize,
	const unsigned char* in, size_t insize,
	const LodePNGDecompressSettings* settings) {

	if (out == nullptr || outsize == nullptr || (in == nullptr && insize != 0)) {
		return 52;
	}
	*out = nullptr;
	*outsize = 0;

	const size_t cap = decompressLimit(settings);
	libdeflate_decompressor* decompressor = libdeflate_alloc_decompressor();
	if (!decompressor) return 83; // lodepng alloc failure code
	auto* buf = static_cast<unsigned char*>(malloc(cap ? cap : 1));
	if (!buf) {
		libdeflate_free_decompressor(decompressor);
		return 83;
	}

	size_t actual_in = 0;
	size_t actual_out = 0;
	const libdeflate_result result = libdeflate_zlib_decompress_ex(
		decompressor, in, insize, buf, cap, &actual_in, &actual_out);
	libdeflate_free_decompressor(decompressor);

	if (result != LIBDEFLATE_SUCCESS || actual_in != insize) {
		free(buf);
		if (result == LIBDEFLATE_INSUFFICIENT_SPACE) return ERROR_OUTPUT_LIMIT;
		// A complete stream followed by more bytes: see ERROR_TRAILING_DATA.
		return result == LIBDEFLATE_SUCCESS ? ERROR_TRAILING_DATA : 52;
	}
	// Valid PNGs fill the exactly sized buffer; avoid a redundant realloc.
	if (actual_out != cap) {
		auto* shrunk = static_cast<unsigned char*>(realloc(buf, actual_out ? actual_out : 1));
		if (shrunk) buf = shrunk;
	}
	*out = buf;
	*outsize = actual_out;
	return 0;
}

// Compress callback for lodepng (zlib format).
inline unsigned compress(
	unsigned char** out, size_t* outsize,
	const unsigned char* in, size_t insize,
	const LodePNGCompressSettings* /*settings*/) {

	if (out == nullptr || outsize == nullptr || (in == nullptr && insize != 0)) {
		return 52;
	}
	*out = nullptr;
	*outsize = 0;

	// libdeflate level 6 matches zlib's Z_DEFAULT_COMPRESSION intent. lodepng
	// owns the returned buffer and frees it with free(), so it must be a
	// malloc allocation — libdeflate writes into a buffer we provide, so that
	// contract is preserved.
	libdeflate_compressor* compressor = libdeflate_alloc_compressor(6);
	if (!compressor) return 83;

	const size_t bound = libdeflate_zlib_compress_bound(compressor, insize);
	auto* buf = static_cast<unsigned char*>(malloc(bound));
	if (!buf) {
		libdeflate_free_compressor(compressor);
		return 83;
	}

	const size_t produced = libdeflate_zlib_compress(compressor, in, insize, buf, bound);
	libdeflate_free_compressor(compressor);
	if (produced == 0) {
		free(buf);
		return 52;
	}

	*outsize = produced;

	// Shrink to exact size. Use max(1, size) to avoid realloc(ptr,0) UB in C17+.
	auto* final_buf = static_cast<unsigned char*>(realloc(buf, *outsize ? *outsize : 1));
	*out = final_buf ? final_buf : buf;

	return 0;
}

// Configure a lodepng decoder State to use the libdeflate adapter.
inline void configureDecoder(lodepng::State& state) {
	state.decoder.zlibsettings.custom_zlib = decompress;
}

// Configure a lodepng encoder State to use the libdeflate adapter.
inline void configureEncoder(lodepng::State& state) {
	state.encoder.zlibsettings.custom_zlib = compress;
}

} // namespace lodepng_zlib_adapter
