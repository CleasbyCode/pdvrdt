// Adapter that bridges lodepng's custom_zlib callbacks to system zlib.
// Used when LODEPNG_NO_COMPILE_ZLIB is defined.

#pragma once

#include "lodepng_config.h"
#include "lodepng.h"
#include <cstdlib>
#include <limits>
#include <zlib.h>

namespace lodepng_zlib_adapter {

// Decompress callback for lodepng (zlib format: header + deflate + adler32).
inline unsigned decompress(
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

	if (insize > static_cast<size_t>(std::numeric_limits<uLong>::max())) {
		return 52;
	}

	uLongf compressed_size = compressBound(static_cast<uLong>(insize));
	auto* buf = static_cast<unsigned char*>(malloc(compressed_size));
	if (!buf) return 83;

	int ret = compress2(buf, &compressed_size, in, static_cast<uLong>(insize), Z_DEFAULT_COMPRESSION);
	if (ret != Z_OK) {
		free(buf);
		return 52;
	}

	*outsize = static_cast<size_t>(compressed_size);

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
