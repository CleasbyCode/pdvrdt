// Adapter that bridges lodepng's custom_zlib callbacks to system zlib.
// Used when LODEPNG_NO_COMPILE_ZLIB is defined.

#pragma once

#include "../common.h"

namespace lodepng_zlib_adapter {

// Decompress callback for lodepng (zlib format: header + deflate + adler32).
inline unsigned decompress(
	unsigned char** out, size_t* outsize,
	const unsigned char* in, size_t insize,
	const LodePNGDecompressSettings* /*settings*/) {

	// Start with 4x input as an estimate, grow if needed.
	size_t bufsize = insize * 4 + 1024;
	auto* buf = static_cast<unsigned char*>(malloc(bufsize));
	if (!buf) return 83; // lodepng alloc failure code

	z_stream strm{};
	strm.next_in  = const_cast<unsigned char*>(in);
	strm.avail_in = static_cast<uInt>(insize);

	if (inflateInit(&strm) != Z_OK) {
		free(buf);
		return 52; // lodepng zlib error code
	}

	strm.next_out  = buf;
	strm.avail_out = static_cast<uInt>(bufsize);

	while (true) {
		int ret = inflate(&strm, Z_NO_FLUSH);

		if (ret == Z_STREAM_END) break;

		if (ret == Z_BUF_ERROR || (ret == Z_OK && strm.avail_out == 0)) {
			// Need more output space.
			size_t have = bufsize - strm.avail_out;
			bufsize *= 2;
			auto* newbuf = static_cast<unsigned char*>(realloc(buf, bufsize));
			if (!newbuf) {
				inflateEnd(&strm);
				free(buf);
				return 83;
			}
			buf = newbuf;
			strm.next_out  = buf + have;
			strm.avail_out = static_cast<uInt>(bufsize - have);
			continue;
		}

		if (ret != Z_OK) {
			inflateEnd(&strm);
			free(buf);
			return 52;
		}
	}

	*outsize = strm.total_out;
	inflateEnd(&strm);

	// Shrink to exact size.
	auto* final_buf = static_cast<unsigned char*>(realloc(buf, *outsize));
	*out = final_buf ? final_buf : buf;

	return 0;
}

// Compress callback for lodepng (zlib format).
inline unsigned compress(
	unsigned char** out, size_t* outsize,
	const unsigned char* in, size_t insize,
	const LodePNGCompressSettings* /*settings*/) {

	uLongf compressed_size = compressBound(static_cast<uLong>(insize));
	auto* buf = static_cast<unsigned char*>(malloc(compressed_size));
	if (!buf) return 83;

	int ret = compress2(buf, &compressed_size, in, static_cast<uLong>(insize), Z_DEFAULT_COMPRESSION);
	if (ret != Z_OK) {
		free(buf);
		return 52;
	}

	*outsize = static_cast<size_t>(compressed_size);

	// Shrink to exact size.
	auto* final_buf = static_cast<unsigned char*>(realloc(buf, *outsize));
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
