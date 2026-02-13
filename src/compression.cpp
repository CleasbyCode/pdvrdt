#include "compression.h"

namespace {

constexpr std::size_t ZLIB_BUFSIZE = 2 * 1024 * 1024;

struct ZlibDeflateGuard {
	z_stream strm{};
	bool initialized = false;

	explicit ZlibDeflateGuard(int level) {
		if (deflateInit(&strm, level) != Z_OK) {
			throw std::runtime_error("zlib deflateInit failed.");
		}
		initialized = true;
	}

	~ZlibDeflateGuard() { if (initialized) deflateEnd(&strm); }

	ZlibDeflateGuard(const ZlibDeflateGuard&) = delete;
	ZlibDeflateGuard& operator=(const ZlibDeflateGuard&) = delete;
};

struct ZlibInflateGuard {
	z_stream strm{};
	bool initialized = false;

	ZlibInflateGuard() {
		if (inflateInit(&strm) != Z_OK) {
			throw std::runtime_error("zlib inflateInit failed.");
		}
		initialized = true;
	}

	~ZlibInflateGuard() { if (initialized) inflateEnd(&strm); }

	ZlibInflateGuard(const ZlibInflateGuard&) = delete;
	ZlibInflateGuard& operator=(const ZlibInflateGuard&) = delete;
};

} // namespace

void zlibDeflate(vBytes& data_vec, Option option, bool is_compressed_file) {
	const int level = [&] {
		if (option == Option::Mastodon)  return Z_DEFAULT_COMPRESSION;
		if (is_compressed_file)          return Z_NO_COMPRESSION;
		return Z_BEST_COMPRESSION;
	}();

	ZlibDeflateGuard guard(level);
	z_stream& strm = guard.strm;

	vBytes buffer(ZLIB_BUFSIZE);
	vBytes result;
	result.reserve(data_vec.size());

	strm.next_in  = data_vec.data();
	strm.avail_in = static_cast<uInt>(std::min(data_vec.size(), static_cast<std::size_t>(std::numeric_limits<uInt>::max())));

	auto flush_output = [&] {
		const std::size_t written = ZLIB_BUFSIZE - strm.avail_out;
		if (written > 0) {
			result.insert(result.end(), buffer.begin(), buffer.begin() + written);
		}
		strm.next_out  = buffer.data();
		strm.avail_out = static_cast<uInt>(ZLIB_BUFSIZE);
	};

	int flush = (strm.avail_in == 0) ? Z_FINISH : Z_NO_FLUSH;

	while (true) {
		strm.next_out  = buffer.data();
		strm.avail_out = static_cast<uInt>(ZLIB_BUFSIZE);

		int ret = deflate(&strm, flush);
		if (ret == Z_STREAM_ERROR) {
			throw std::runtime_error("zlib deflate failed.");
		}

		flush_output();

		if (ret == Z_STREAM_END) break;

		if (strm.avail_in == 0 && flush == Z_NO_FLUSH) {
			flush = Z_FINISH;
		}
	}

	data_vec = std::move(result);
}

void zlibInflate(vBytes& data_vec) {
	ZlibInflateGuard guard;
	z_stream& strm = guard.strm;

	vBytes buffer(ZLIB_BUFSIZE);
	vBytes result;
	result.reserve(data_vec.size() * 2);

	strm.next_in  = data_vec.data();
	strm.avail_in = static_cast<uInt>(std::min(data_vec.size(), static_cast<std::size_t>(std::numeric_limits<uInt>::max())));

	auto flush_output = [&] {
		const std::size_t written = ZLIB_BUFSIZE - strm.avail_out;
		if (written > 0) {
			result.insert(result.end(), buffer.begin(), buffer.begin() + written);
		}
		strm.next_out  = buffer.data();
		strm.avail_out = static_cast<uInt>(ZLIB_BUFSIZE);
	};

	while (true) {
		strm.next_out  = buffer.data();
		strm.avail_out = static_cast<uInt>(ZLIB_BUFSIZE);

		const int flush = (strm.avail_in > 0) ? Z_NO_FLUSH : Z_FINISH;
		const int ret = inflate(&strm, flush);

		flush_output();

		if (ret == Z_STREAM_END) break;

		if (ret == Z_BUF_ERROR) break;
		if (ret == Z_OK) continue;

		throw std::runtime_error(std::format(
			"zlib inflate failed: {}",
			strm.msg ? strm.msg : std::to_string(ret)
		));
	}

	data_vec = std::move(result);
}
