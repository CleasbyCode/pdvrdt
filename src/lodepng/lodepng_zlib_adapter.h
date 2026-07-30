// Adapter that bridges lodepng's custom_zlib callbacks to system libraries:
// decompress via zlib inflate, compress via libdeflate (whole-buffer, much
// faster than zlib at equivalent ratio). Used when LODEPNG_NO_COMPILE_ZLIB
// is defined.

#pragma once

#include "lodepng_config.h"
#include "lodepng.h"
#include <cstdlib>
#include <limits>
#include <libdeflate.h>
#include <zlib.h>

namespace lodepng_zlib_adapter {

// Streaming zlib inflate. Fallback for the libdeflate fast path below: used when
// the whole-buffer size estimate was too small, growing its output buffer
// without ever re-decompressing.
inline unsigned decompressZlibStreaming(
	unsigned char** out, size_t* outsize,
	const unsigned char* in, size_t insize,
	const LodePNGDecompressSettings* /*settings*/) {

	if (out == nullptr || outsize == nullptr || (in == nullptr && insize != 0)) {
		return 52;
	}
	*out = nullptr;
	*outsize = 0;

	// Bound output to prevent decompression bombs.
	// Max realistic PNG pixel data ~134MB (4096*4096*RGBA*16bit + filter bytes).
	// 256MB gives ample headroom.
	constexpr size_t MAX_DECOMPRESS_SIZE = 256ULL * 1024 * 1024;

	// Overflow-safe initial buffer estimate.
	size_t bufsize = (insize <= (MAX_DECOMPRESS_SIZE - 1024) / 4)
		? insize * 4 + 1024
		: 4ULL * 1024 * 1024;

	auto* buf = static_cast<unsigned char*>(malloc(bufsize));
	if (!buf) return 83; // lodepng alloc failure code

	z_stream strm{};
	if (inflateInit(&strm) != Z_OK) {
		free(buf);
		return 52; // lodepng zlib error code
	}

	auto fail = [&]() -> unsigned {
		inflateEnd(&strm);
		free(buf);
		return 52;
	};

	// Feed input in uInt-sized chunks to avoid truncation on platforms
	// where uInt is narrower than size_t.
	constexpr size_t MAX_UINT = static_cast<size_t>(static_cast<uInt>(-1));
	size_t in_pos = 0;
	auto feed_input = [&]() -> bool {
		if (strm.avail_in > 0 || in_pos >= insize) return false;
		size_t remain = insize - in_pos;
		size_t chunk = (remain > MAX_UINT) ? MAX_UINT : remain;
		strm.next_in = const_cast<unsigned char*>(in + in_pos);
		strm.avail_in = static_cast<uInt>(chunk);
		in_pos += chunk;
		return true;
	};

	feed_input();
	strm.next_out  = buf;
	strm.avail_out = static_cast<uInt>((bufsize > MAX_UINT) ? MAX_UINT : bufsize);

	while (true) {
		const auto total_in_before = strm.total_in;
		const auto total_out_before = strm.total_out;
		int ret = inflate(&strm, Z_NO_FLUSH);

		if (ret == Z_STREAM_END) {
			if (strm.avail_in != 0 || in_pos != insize) {
				return fail();
			}
			break;
		}

		if (ret != Z_OK && ret != Z_BUF_ERROR) {
			return fail();
		}

		// Grow output buffer if full.
		if (strm.avail_out == 0) {
			size_t have = strm.total_out;
			if (bufsize >= MAX_DECOMPRESS_SIZE) {
				return fail(); // Output exceeds safety limit.
			}
			size_t new_size = (bufsize <= MAX_DECOMPRESS_SIZE / 2)
				? bufsize * 2
				: MAX_DECOMPRESS_SIZE;
			auto* newbuf = static_cast<unsigned char*>(realloc(buf, new_size));
			if (!newbuf) {
				inflateEnd(&strm);
				free(buf);
				return 83;
			}
			buf = newbuf;
			bufsize = new_size;
			strm.next_out  = buf + have;
			size_t out_remain = bufsize - have;
			strm.avail_out = static_cast<uInt>((out_remain > MAX_UINT) ? MAX_UINT : out_remain);
		}

		// Feed more input if needed.
		bool fed_more_input = false;
		if (strm.avail_in == 0) {
			fed_more_input = feed_input();
			// No input left, output has space, and no progress: truncated stream.
			if (!fed_more_input && strm.avail_out > 0 && ret == Z_BUF_ERROR) {
				return fail();
			}
		}

		if (ret == Z_BUF_ERROR &&
			!fed_more_input &&
			total_in_before == strm.total_in &&
			total_out_before == strm.total_out) {
			return fail();
		}
	}

	*outsize = strm.total_out;
	inflateEnd(&strm);

	// Shrink to exact size. Use max(1, size) to avoid realloc(ptr,0) UB in C17+.
	auto* final_buf = static_cast<unsigned char*>(realloc(buf, *outsize ? *outsize : 1));
	*out = final_buf ? final_buf : buf;

	return 0;
}

// Decompress callback for lodepng (zlib format: header + deflate + adler32).
// Fast path: libdeflate whole-buffer decompress, substantially faster than zlib
// inflate. libdeflate is non-streaming and needs the output buffer sized up
// front, but the zlib format carries no uncompressed-size field, so we estimate.
// If the estimate is too small we fall back to the streaming inflate above
// (never re-decompressing). Over-estimation is virtual memory (lazily committed),
// so only the bytes libdeflate actually writes are paged in. This mirrors the
// "libdeflate fast path + zlib fallback" split used in compression.cpp.
inline unsigned decompress(
	unsigned char** out, size_t* outsize,
	const unsigned char* in, size_t insize,
	const LodePNGDecompressSettings* settings) {

	if (out == nullptr || outsize == nullptr || (in == nullptr && insize != 0)) {
		return 52;
	}
	*out = nullptr;
	*outsize = 0;

	constexpr size_t MAX_DECOMPRESS_SIZE = 256ULL * 1024 * 1024;

	const size_t cap = (insize <= (MAX_DECOMPRESS_SIZE - 1024) / 4)
		? insize * 4 + 1024
		: MAX_DECOMPRESS_SIZE;

	if (libdeflate_decompressor* decompressor = libdeflate_alloc_decompressor()) {
		if (auto* buf = static_cast<unsigned char*>(malloc(cap))) {
			size_t actual_in = 0;
			size_t actual_out = 0;
			const libdeflate_result result = libdeflate_zlib_decompress_ex(
				decompressor, in, insize, buf, cap, &actual_in, &actual_out);
			libdeflate_free_decompressor(decompressor);

			if (result == LIBDEFLATE_SUCCESS) {
				// Require the whole input to be a single zlib stream with no
				// trailing bytes, matching the streaming path's strictness.
				if (actual_in != insize || actual_out > MAX_DECOMPRESS_SIZE) {
					free(buf);
					return 52;
				}
				auto* shrunk = static_cast<unsigned char*>(realloc(buf, actual_out ? actual_out : 1));
				*out = shrunk ? shrunk : buf;
				*outsize = actual_out;
				return 0;
			}

			free(buf);
			if (result != LIBDEFLATE_INSUFFICIENT_SPACE) {
				return 52;  // corrupt/short stream — a real error, not an estimate miss
			}
			// Estimate too small: fall through to the streaming inflate.
		} else {
			libdeflate_free_decompressor(decompressor);
		}
	}

	return decompressZlibStreaming(out, outsize, in, insize, settings);
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

// Configure a lodepng decoder State to use system zlib.
inline void configureDecoder(lodepng::State& state) {
	state.decoder.zlibsettings.custom_zlib = decompress;
}

// Configure a lodepng encoder State to use system zlib.
inline void configureEncoder(lodepng::State& state) {
	state.encoder.zlibsettings.custom_zlib = compress;
}

} // namespace lodepng_zlib_adapter
