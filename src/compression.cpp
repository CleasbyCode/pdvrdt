#include "compression.h"
#include "io_utils.h"

#include <zlib.h>

#include <algorithm>
#include <cerrno>
#include <format>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
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

[[nodiscard]] std::size_t inflateReserveHint(std::size_t input_size, std::size_t max_output_size) {
	if (max_output_size == 0) return 0;
	const std::size_t capped_limit = std::min(max_output_size, MAX_INFLATE_INITIAL_RESERVE);
	std::size_t hint = std::min(std::max(input_size, MIN_INFLATE_INITIAL_RESERVE), capped_limit);
	if (hint <= capped_limit / 2) hint *= 2;
	return hint;
}

template <typename OutputHandler>
void inflateDriver(std::span<const Byte> input, std::size_t max_output_size, OutputHandler&& on_output) {
	ZlibInflateGuard guard;
	z_stream& strm = guard.strm;
	vBytes buffer(ZLIB_BUFSIZE);
	std::size_t total_output = 0;
	std::size_t input_offset = 0;

	auto refill_input = [&] {
		if (strm.avail_in != 0 || input_offset >= input.size()) return;
		const std::size_t chunk_size = std::min<std::size_t>(
			input.size() - input_offset,
			static_cast<std::size_t>(std::numeric_limits<uInt>::max())
		);
		strm.next_in = const_cast<Byte*>(input.data() + static_cast<std::ptrdiff_t>(input_offset));
		strm.avail_in = static_cast<uInt>(chunk_size);
		input_offset += chunk_size;
	};

	bool saw_stream_end = false;
	while (true) {
		refill_input();
		strm.next_out = buffer.data();
		strm.avail_out = static_cast<uInt>(ZLIB_BUFSIZE);

		const auto total_in_before = strm.total_in;
		const auto total_out_before = strm.total_out;
		const int ret = inflate(&strm, Z_NO_FLUSH);
		const std::size_t produced = ZLIB_BUFSIZE - strm.avail_out;
		const bool made_progress =
			strm.total_in != total_in_before ||
			strm.total_out != total_out_before;

		if (produced > 0) {
			if (produced > max_output_size || total_output > max_output_size - produced) {
				throw std::runtime_error("Zlib Compression Error: Inflated data exceeds maximum program size limit.");
			}
			on_output(buffer.data(), produced);
			total_output += produced;
		}

		if (ret == Z_STREAM_END) { saw_stream_end = true; break; }
		if (ret == Z_OK) {
			if (!made_progress) {
				throw std::runtime_error("zlib inflate failed: stalled stream.");
			}
			continue;
		}
		if (ret == Z_BUF_ERROR) {
			if (strm.avail_in == 0) {
				refill_input();
				if (strm.avail_in != 0) continue;
			}
			if (strm.avail_out == 0) continue;
			throw std::runtime_error("zlib inflate failed: stalled stream.");
		}
		throw std::runtime_error(std::format(
			"zlib inflate failed: {}",
			strm.msg ? strm.msg : std::to_string(ret)));
	}

	if (!saw_stream_end) throw std::runtime_error("zlib inflate failed: truncated stream.");
	if (strm.avail_in != 0 || input_offset != input.size()) {
		throw std::runtime_error("zlib inflate failed: trailing data after stream end.");
	}
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

template <typename RefillInput>
void deflateDriver(Option option, bool is_compressed_file, const DeflateChunkHandler& on_chunk, RefillInput&& refill_input) {
	ZlibDeflateGuard guard(deflateCompressionLevel(option, is_compressed_file));
	z_stream& strm = guard.strm;
	vBytes output_buffer(ZLIB_BUFSIZE);

	while (true) {
		const bool reached_eof = refill_input(strm);

		strm.next_out = output_buffer.data();
		strm.avail_out = static_cast<uInt>(output_buffer.size());

		const auto total_in_before = strm.total_in;
		const auto total_out_before = strm.total_out;
		const int ret = deflate(&strm, reached_eof ? Z_FINISH : Z_NO_FLUSH);
		const std::size_t written = output_buffer.size() - strm.avail_out;
		const bool made_progress =
			strm.total_in != total_in_before ||
			strm.total_out != total_out_before;
		if (written != 0) {
			on_chunk(std::span<const Byte>(output_buffer.data(), written));
		}
		if (ret == Z_STREAM_END) {
			return;
		}
		if (ret == Z_OK && !made_progress) {
			throw std::runtime_error("zlib deflate failed: stalled stream.");
		}
		if (ret != Z_OK) {
			throw std::runtime_error(std::format(
				"zlib deflate failed: {}",
				strm.msg ? strm.msg : std::to_string(ret)));
		}
	}
}

} // namespace

void zlibDeflateSpan(std::span<const Byte> data, Option option, bool is_compressed_file, const DeflateChunkHandler& on_chunk) {
	if (!on_chunk) {
		throw std::invalid_argument("zlibDeflateSpan: output handler is required.");
	}

	std::size_t input_offset = 0;

	deflateDriver(option, is_compressed_file, on_chunk, [&](z_stream& strm) {
		if (strm.avail_in == 0 && input_offset < data.size()) {
			const std::size_t chunk_size = std::min<std::size_t>(
				data.size() - input_offset,
				static_cast<std::size_t>(std::numeric_limits<uInt>::max())
			);
			strm.next_in = const_cast<Byte*>(data.data() + static_cast<std::ptrdiff_t>(input_offset));
			strm.avail_in = static_cast<uInt>(chunk_size);
			input_offset += chunk_size;
		}
		return input_offset == data.size() && strm.avail_in == 0;
	});
}

void zlibDeflateFile(const fs::path& path, Option option, bool is_compressed_file, const DeflateChunkHandler& on_chunk) {
	if (!on_chunk) {
		throw std::invalid_argument("zlibDeflateFile: output handler is required.");
	}

	std::ifstream file(path, std::ios::binary);
	if (!file) {
		throw std::runtime_error(std::format("Failed to open file: {}", path.string()));
	}

	vBytes input_buffer(ZLIB_BUFSIZE);
	bool reached_eof = false;

	deflateDriver(option, is_compressed_file, on_chunk, [&](z_stream& strm) {
		if (strm.avail_in == 0 && !reached_eof) {
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
			} else {
				strm.next_in = input_buffer.data();
				strm.avail_in = static_cast<uInt>(bytes_read);
				reached_eof = file.eof();
				if (!file && !file.eof()) {
					throw std::runtime_error("Failed to read full file: partial read");
				}
			}
		}
		return reached_eof && strm.avail_in == 0;
	});
}

vBytes zlibInflateSpanBounded(std::span<const Byte> data, std::size_t max_output_size) {
	vBytes result;
	result.reserve(inflateReserveHint(data.size(), max_output_size));
	inflateDriver(data, max_output_size, [&](const Byte* buf, std::size_t len) {
		result.insert(result.end(), buf, buf + len);
	});
	return result;
}

std::size_t zlibInflateToFd(const vBytes& data_vec, int fd) {
	std::size_t total_written = 0;
	inflateDriver(data_vec, MAX_INFLATED_OUTPUT_SIZE, [&](const Byte* buf, std::size_t len) {
		writeAllToFd(fd, std::span<const Byte>(buf, len));
		total_written += len;
	});
	if (total_written == 0) {
		throw std::runtime_error("Zlib Compression Error: Output file is empty. Inflating file failed.");
	}
	return total_written;
}
