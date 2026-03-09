#include "compression.h"
#include "io_utils.h"

#include <zlib.h>

#include <algorithm>
#include <cerrno>
#include <format>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unistd.h>

namespace {

constexpr std::size_t ZLIB_BUFSIZE = 2 * 1024 * 1024;
constexpr std::size_t MAX_INFLATED_OUTPUT_SIZE = 3ULL * 1024 * 1024 * 1024;
constexpr std::size_t MIN_INFLATE_INITIAL_RESERVE = 256 * 1024;
constexpr std::size_t MAX_INFLATE_INITIAL_RESERVE = 64ULL * 1024 * 1024;

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

void appendOutputChunk(vBytes& result, std::span<const Byte> buffer, std::size_t written, std::size_t max_output_size) {
	if (written == 0) {
		return;
	}
	if (written > max_output_size || result.size() > max_output_size - written) {
		throw std::runtime_error("Zlib Compression Error: Inflated data exceeds maximum program size limit.");
	}
	result.insert(result.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(written));
}

[[nodiscard]] std::size_t inflateReserveHint(std::size_t input_size, std::size_t max_output_size) {
	if (max_output_size == 0) {
		return 0;
	}

	const std::size_t capped_limit = std::min(max_output_size, MAX_INFLATE_INITIAL_RESERVE);
	std::size_t hint = std::min(
		std::max(input_size, MIN_INFLATE_INITIAL_RESERVE),
		capped_limit
	);
	if (hint <= capped_limit / 2) {
		hint *= 2;
	}
	return hint;
}

[[nodiscard]] vBytes inflateToVector(std::span<const Byte> input, std::size_t max_output_size) {
	ZlibInflateGuard guard;
	z_stream& strm = guard.strm;

	vBytes buffer(ZLIB_BUFSIZE);
	vBytes result;
	result.reserve(inflateReserveHint(input.size(), max_output_size));

	std::size_t input_offset = 0;
	auto refill_input = [&] {
		if (strm.avail_in != 0 || input_offset >= input.size()) {
			return;
		}
		const std::size_t chunk_size = std::min<std::size_t>(
			input.size() - input_offset,
			static_cast<std::size_t>(std::numeric_limits<uInt>::max())
		);
		strm.next_in = const_cast<Byte*>(input.data() + static_cast<std::ptrdiff_t>(input_offset));
		strm.avail_in = static_cast<uInt>(chunk_size);
		input_offset += chunk_size;
	};

	auto flush_output = [&] {
		const std::size_t written = ZLIB_BUFSIZE - strm.avail_out;
		appendOutputChunk(result, std::span<const Byte>(buffer.data(), written), written, max_output_size);
		strm.next_out  = buffer.data();
		strm.avail_out = static_cast<uInt>(ZLIB_BUFSIZE);
	};

	while (true) {
		refill_input();

		strm.next_out  = buffer.data();
		strm.avail_out = static_cast<uInt>(ZLIB_BUFSIZE);

		const int ret = inflate(&strm, Z_NO_FLUSH);

		flush_output();

		if (ret == Z_STREAM_END) {
			break;
		}
		if (ret == Z_OK) {
			continue;
		}
		if (ret == Z_BUF_ERROR) {
			if (strm.avail_in == 0) {
				refill_input();
				if (strm.avail_in != 0) {
					continue;
				}
				if (input_offset == input.size()) {
					// Preserve compatibility with existing files where zlib reports
					// Z_BUF_ERROR at end-of-input instead of Z_STREAM_END.
					break;
				}
			}
			if (strm.avail_out == 0) {
				continue;
			}
			throw std::runtime_error("zlib inflate failed: stalled stream.");
		}

		throw std::runtime_error(std::format(
			"zlib inflate failed: {}",
			strm.msg ? strm.msg : std::to_string(ret)
		));
	}

	return result;
}

[[nodiscard]] std::size_t inflateToFd(std::span<const Byte> input, int fd, std::size_t max_output_size) {
	ZlibInflateGuard guard;
	z_stream& strm = guard.strm;

	vBytes buffer(ZLIB_BUFSIZE);
	std::size_t total_written = 0;

	std::size_t input_offset = 0;
	auto refill_input = [&] {
		if (strm.avail_in != 0 || input_offset >= input.size()) {
			return;
		}
		const std::size_t chunk_size = std::min<std::size_t>(
			input.size() - input_offset,
			static_cast<std::size_t>(std::numeric_limits<uInt>::max())
		);
		strm.next_in = const_cast<Byte*>(input.data() + static_cast<std::ptrdiff_t>(input_offset));
		strm.avail_in = static_cast<uInt>(chunk_size);
		input_offset += chunk_size;
	};

	auto flush_output = [&] {
		const std::size_t produced = ZLIB_BUFSIZE - strm.avail_out;
		if (produced == 0) {
			return;
		}
		if (produced > max_output_size || total_written > max_output_size - produced) {
			throw std::runtime_error("Zlib Compression Error: Inflated data exceeds maximum program size limit.");
		}
		writeAllToFd(fd, std::span<const Byte>(buffer.data(), produced));
		total_written += produced;
		strm.next_out = buffer.data();
		strm.avail_out = static_cast<uInt>(ZLIB_BUFSIZE);
	};

	while (true) {
		refill_input();

		strm.next_out = buffer.data();
		strm.avail_out = static_cast<uInt>(ZLIB_BUFSIZE);

		const int ret = inflate(&strm, Z_NO_FLUSH);

		flush_output();

		if (ret == Z_STREAM_END) {
			break;
		}
		if (ret == Z_OK) {
			continue;
		}
		if (ret == Z_BUF_ERROR) {
			if (strm.avail_in == 0) {
				refill_input();
				if (strm.avail_in != 0) {
					continue;
				}
				if (input_offset == input.size()) {
					// Preserve compatibility with existing files where zlib reports
					// Z_BUF_ERROR at end-of-input instead of Z_STREAM_END.
					break;
				}
			}
			if (strm.avail_out == 0) {
				continue;
			}
			throw std::runtime_error("zlib inflate failed: stalled stream.");
		}

		throw std::runtime_error(std::format(
			"zlib inflate failed: {}",
			strm.msg ? strm.msg : std::to_string(ret)
		));
	}

	return total_written;
}

[[nodiscard]] int deflateCompressionLevel(Option option, bool is_compressed_file) {
	if (option == Option::Mastodon) {
		return Z_DEFAULT_COMPRESSION;
	}
	if (is_compressed_file) {
		return Z_NO_COMPRESSION;
	}
	return Z_BEST_COMPRESSION;
}

} // namespace

void zlibDeflate(vBytes& data_vec, Option option, bool is_compressed_file) {
	ZlibDeflateGuard guard(deflateCompressionLevel(option, is_compressed_file));
	z_stream& strm = guard.strm;

	vBytes buffer(ZLIB_BUFSIZE);
	vBytes result;
	const uLong bounded_input = static_cast<uLong>(
		std::min(data_vec.size(), static_cast<std::size_t>(std::numeric_limits<uLong>::max()))
	);
	const uLong bound = deflateBound(&strm, bounded_input);
	if (bound <= static_cast<uLong>(std::numeric_limits<std::size_t>::max())) {
		result.reserve(static_cast<std::size_t>(bound));
	}

	strm.next_in  = data_vec.data();
	strm.avail_in = static_cast<uInt>(std::min(data_vec.size(), static_cast<std::size_t>(std::numeric_limits<uInt>::max())));

	auto flush_output = [&] {
		const std::size_t written = ZLIB_BUFSIZE - strm.avail_out;
		appendOutputChunk(result, std::span<const Byte>(buffer), written, std::numeric_limits<std::size_t>::max());
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

void zlibDeflateSpan(std::span<const Byte> data, Option option, bool is_compressed_file, const DeflateChunkHandler& on_chunk) {
	if (!on_chunk) {
		throw std::invalid_argument("zlibDeflateSpan: output handler is required.");
	}

	ZlibDeflateGuard guard(deflateCompressionLevel(option, is_compressed_file));
	z_stream& strm = guard.strm;

	vBytes output_buffer(ZLIB_BUFSIZE);
	std::size_t input_offset = 0;

	auto refill_input = [&] {
		if (strm.avail_in != 0 || input_offset >= data.size()) {
			return;
		}

		const std::size_t chunk_size = std::min<std::size_t>(
			data.size() - input_offset,
			static_cast<std::size_t>(std::numeric_limits<uInt>::max())
		);
		strm.next_in = const_cast<Byte*>(data.data() + static_cast<std::ptrdiff_t>(input_offset));
		strm.avail_in = static_cast<uInt>(chunk_size);
		input_offset += chunk_size;
	};

	while (true) {
		refill_input();

		strm.next_out = output_buffer.data();
		strm.avail_out = static_cast<uInt>(output_buffer.size());

		const int flush = (input_offset == data.size() && strm.avail_in == 0) ? Z_FINISH : Z_NO_FLUSH;
		const int ret = deflate(&strm, flush);
		if (ret == Z_STREAM_ERROR) {
			throw std::runtime_error("zlib deflate failed.");
		}

		const std::size_t written = output_buffer.size() - strm.avail_out;
		if (written != 0) {
			on_chunk(std::span<const Byte>(output_buffer.data(), written));
		}

		if (ret == Z_STREAM_END) {
			break;
		}
	}
}

void zlibDeflateFile(const fs::path& path, Option option, bool is_compressed_file, const DeflateChunkHandler& on_chunk) {
	if (!on_chunk) {
		throw std::invalid_argument("zlibDeflateFile: output handler is required.");
	}

	std::ifstream file(path, std::ios::binary);
	if (!file) {
		throw std::runtime_error(std::format("Failed to open file: {}", path.string()));
	}

	ZlibDeflateGuard guard(deflateCompressionLevel(option, is_compressed_file));
	z_stream& strm = guard.strm;

	vBytes input_buffer(ZLIB_BUFSIZE);
	vBytes output_buffer(ZLIB_BUFSIZE);
	bool reached_eof = false;

	auto refill_input = [&] {
		if (strm.avail_in != 0 || reached_eof) {
			return;
		}

		file.read(
			reinterpret_cast<char*>(input_buffer.data()),
			static_cast<std::streamsize>(input_buffer.size())
		);
		const std::streamsize bytes_read = file.gcount();
		if (bytes_read == 0) {
			if (!file.eof()) {
				throw std::runtime_error("Failed to read full file: partial read");
			}
			reached_eof = true;
			return;
		}

		strm.next_in = input_buffer.data();
		strm.avail_in = static_cast<uInt>(bytes_read);
		reached_eof = file.eof();
		if (!file && !file.eof()) {
			throw std::runtime_error("Failed to read full file: partial read");
		}
	};

	while (true) {
		refill_input();

		strm.next_out = output_buffer.data();
		strm.avail_out = static_cast<uInt>(output_buffer.size());

		const int flush = (reached_eof && strm.avail_in == 0) ? Z_FINISH : Z_NO_FLUSH;
		const int ret = deflate(&strm, flush);
		if (ret == Z_STREAM_ERROR) {
			throw std::runtime_error("zlib deflate failed.");
		}

		const std::size_t written = output_buffer.size() - strm.avail_out;
		if (written != 0) {
			on_chunk(std::span<const Byte>(output_buffer.data(), written));
		}

		if (ret == Z_STREAM_END) {
			break;
		}
	}
}

void zlibInflate(vBytes& data_vec) {
	data_vec = inflateToVector(std::span<const Byte>(data_vec.data(), data_vec.size()), MAX_INFLATED_OUTPUT_SIZE);
}

vBytes zlibInflateSpanBounded(std::span<const Byte> data, std::size_t max_output_size) {
	return inflateToVector(data, max_output_size);
}

std::size_t zlibInflateToFd(const vBytes& data_vec, int fd) {
	const std::size_t total_written = inflateToFd(
		std::span<const Byte>(data_vec.data(), data_vec.size()),
		fd,
		MAX_INFLATED_OUTPUT_SIZE
	);

	if (total_written == 0) {
		throw std::runtime_error("Zlib Compression Error: Output file is empty. Inflating file failed.");
	}
	return total_written;
}
