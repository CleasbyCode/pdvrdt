#include "compression.h"
#include "io_utils.h"

#include <libdeflate.h>
#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {

constexpr std::size_t ZLIB_BUFSIZE = 2 * 1024 * 1024;
constexpr std::size_t MAX_INFLATED_OUTPUT_SIZE = 3ULL * 1024 * 1024 * 1024;
constexpr std::size_t MIN_INFLATE_INITIAL_RESERVE = 256 * 1024;
constexpr std::size_t MAX_INFLATE_INITIAL_RESERVE = 64ULL * 1024 * 1024;

[[nodiscard]] std::size_t zlibChunkSize(std::size_t remaining) {
	return std::min<std::size_t>(
		remaining,
		static_cast<std::size_t>(std::numeric_limits<uInt>::max())
	);
}

void setZlibInput(z_stream& strm, const Byte* data, std::size_t size) {
	strm.next_in = const_cast<Byte*>(data);
	strm.avail_in = static_cast<uInt>(size);
}

[[nodiscard]] std::string zlibErrorMessage(const z_stream& strm, int ret) {
	return strm.msg ? std::string(strm.msg) : std::to_string(ret);
}

[[noreturn]] void throwZlibError(std::string_view operation, const z_stream& strm, int ret) {
	throw std::runtime_error(std::format("zlib {} failed: {}", operation, zlibErrorMessage(strm, ret)));
}

struct ZlibDeflateGuard {
	z_stream strm{};
	bool initialized = false;

	explicit ZlibDeflateGuard(int level) {
		if (deflateInit(&strm, level) != Z_OK) {
			throw std::runtime_error("zlib deflateInit failed.");
		}
		initialized = true;
	}

	~ZlibDeflateGuard() {
		if (initialized) deflateEnd(&strm);
	}

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

	~ZlibInflateGuard() {
		if (initialized) inflateEnd(&strm);
	}

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

void appendBytes(vBytes& out, std::span<const Byte> bytes) {
	if (bytes.empty()) return;

	const std::size_t base = out.size();
	const std::size_t final_size = checkedAddSize(
		base,
		bytes.size(),
		"Zlib Compression Error: Inflated output size overflow."
	);
	out.resize(final_size);
	std::memcpy(out.data() + static_cast<std::ptrdiff_t>(base), bytes.data(), bytes.size());
}

template <typename OutputHandler>
void inflateDriver(std::span<const Byte> input, std::size_t max_output_size, OutputHandler&& on_output) {
	ZlibInflateGuard guard;
	z_stream& strm = guard.strm;
	auto buffer = std::make_unique_for_overwrite<Byte[]>(ZLIB_BUFSIZE);
	std::size_t total_output = 0;
	std::size_t input_offset = 0;

	auto refill_input = [&] {
		if (strm.avail_in != 0 || input_offset >= input.size()) return;
		const std::size_t chunk_size = zlibChunkSize(input.size() - input_offset);
		setZlibInput(strm, input.data() + static_cast<std::ptrdiff_t>(input_offset), chunk_size);
		input_offset += chunk_size;
	};

	while (true) {
		refill_input();
		strm.next_out = buffer.get();
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
			on_output(buffer.get(), produced);
			total_output += produced;
		}

		if (ret == Z_STREAM_END) break;
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
		throwZlibError("inflate", strm, ret);
	}

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
	// Z_DEFAULT_COMPRESSION (level 6), not Z_BEST_COMPRESSION (9): for the
	// >256 MiB zlib streaming path, level 9 costs far more time for ~1-2% ratio.
	// Kept consistent with the libdeflate fast path below (also level 6).
	return Z_DEFAULT_COMPRESSION;
}

// ----------------------------- libdeflate fast path -----------------------------
//
// Inputs at or below this size are deflated in a single whole-buffer libdeflate
// call (peak RSS ~= input + compressBound, ~2x input). Larger inputs fall back
// to the zlib streaming path, which holds only fixed-size chunks. Both paths
// emit a standard RFC 1950 zlib stream, so the recover-side zlib inflate
// decodes either one.
constexpr std::size_t LIBDEFLATE_WHOLE_BUFFER_LIMIT = 256ULL * 1024 * 1024;

// Map the project's compression-level choice onto a libdeflate level. Measured
// on real corpora, libdeflate L9 costs ~2.6-2.9x the time of L6 for only ~1-2%
// smaller output (L10-12 are far worse). L6 is the ratio/time sweet spot, and it
// matches the level lodepng already re-encodes the PNG IDAT at (see
// lodepng_zlib_adapter::compress), so the secret payload uses 6 as well.
// Do NOT raise this back toward 9-12 without re-measuring.
[[nodiscard]] int libdeflateLevel(Option option, bool is_compressed_file) {
	if (option == Option::Mastodon) {
		return 6;  // matches Z_DEFAULT_COMPRESSION
	}
	if (is_compressed_file) {
		return 0;  // matches Z_NO_COMPRESSION (valid stored zlib stream)
	}
	return 6;      // ratio/time sweet spot; see note above
}

struct LibdeflateCompressorGuard {
	libdeflate_compressor* c{nullptr};
	explicit LibdeflateCompressorGuard(int level) : c(libdeflate_alloc_compressor(level)) {}
	~LibdeflateCompressorGuard() { if (c) libdeflate_free_compressor(c); }
	LibdeflateCompressorGuard(const LibdeflateCompressorGuard&) = delete;
	LibdeflateCompressorGuard& operator=(const LibdeflateCompressorGuard&) = delete;
};

// Whole-buffer zlib-format deflate of `input`, emitted through `on_chunk`.
void libdeflateZlibCompress(std::span<const Byte> input, int level, const DeflateChunkHandler& on_chunk) {
	LibdeflateCompressorGuard compressor(level);
	if (!compressor.c) {
		throw std::runtime_error("libdeflate: failed to allocate compressor.");
	}

	const std::size_t bound = libdeflate_zlib_compress_bound(compressor.c, input.size());
	auto out = std::make_unique_for_overwrite<Byte[]>(bound);

	const std::size_t produced = libdeflate_zlib_compress(
		compressor.c, input.data(), input.size(), out.get(), bound);
	if (produced == 0) {
		throw std::runtime_error("libdeflate: zlib compression failed.");
	}
	on_chunk(std::span<const Byte>(out.get(), produced));
}

// Read an entire regular file into memory given its known size.
[[nodiscard]] vBytes readWholeFile(const fs::path& path, std::size_t size) {
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		throw std::runtime_error(std::format("Failed to open file: {}", path.string()));
	}
	vBytes buffer(size);
	if (size != 0) {
		file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
		if (static_cast<std::size_t>(file.gcount()) != size) {
			throw std::runtime_error("Failed to read full file: partial read");
		}
	}
	return buffer;
}

// Returns the file size if it can be stat'd and is within the whole-buffer
// limit, else nullopt (caller uses the streaming path).
[[nodiscard]] std::optional<std::size_t> wholeBufferEligibleFileSize(const fs::path& path) {
	std::error_code ec;
	const std::uintmax_t size = fs::file_size(path, ec);
	if (ec || size > LIBDEFLATE_WHOLE_BUFFER_LIMIT) {
		return std::nullopt;
	}
	return static_cast<std::size_t>(size);
}

template <typename RefillInput>
void deflateDriver(Option option, bool is_compressed_file, const DeflateChunkHandler& on_chunk, RefillInput&& refill_input) {
	ZlibDeflateGuard guard(deflateCompressionLevel(option, is_compressed_file));
	z_stream& strm = guard.strm;
	auto output_buffer = std::make_unique_for_overwrite<Byte[]>(ZLIB_BUFSIZE);

	while (true) {
		const bool reached_eof = refill_input(strm);

		strm.next_out = output_buffer.get();
		strm.avail_out = static_cast<uInt>(ZLIB_BUFSIZE);

		const auto total_in_before = strm.total_in;
		const auto total_out_before = strm.total_out;
		const int ret = deflate(&strm, reached_eof ? Z_FINISH : Z_NO_FLUSH);
		const std::size_t written = ZLIB_BUFSIZE - strm.avail_out;
		const bool made_progress =
			strm.total_in != total_in_before ||
			strm.total_out != total_out_before;
		if (written != 0) {
			on_chunk(std::span<const Byte>(output_buffer.get(), written));
		}
		if (ret == Z_STREAM_END) {
			return;
		}
		if (ret == Z_OK && !made_progress) {
			throw std::runtime_error("zlib deflate failed: stalled stream.");
		}
		if (ret != Z_OK) {
			throwZlibError("deflate", strm, ret);
		}
	}
}

} // namespace

void zlibDeflateSpan(std::span<const Byte> data, Option option, bool is_compressed_file, const DeflateChunkHandler& on_chunk) {
	if (!on_chunk) {
		throw std::invalid_argument("zlibDeflateSpan: output handler is required.");
	}

	if (data.size() <= LIBDEFLATE_WHOLE_BUFFER_LIMIT) {
		libdeflateZlibCompress(data, libdeflateLevel(option, is_compressed_file), on_chunk);
		return;
	}

	std::size_t input_offset = 0;

	deflateDriver(option, is_compressed_file, on_chunk, [&](z_stream& strm) {
		if (strm.avail_in == 0 && input_offset < data.size()) {
			const std::size_t chunk_size = zlibChunkSize(data.size() - input_offset);
			setZlibInput(strm, data.data() + static_cast<std::ptrdiff_t>(input_offset), chunk_size);
			input_offset += chunk_size;
		}
		return input_offset == data.size() && strm.avail_in == 0;
	});
}

void zlibDeflateFile(const fs::path& path, Option option, bool is_compressed_file, const DeflateChunkHandler& on_chunk) {
	if (!on_chunk) {
		throw std::invalid_argument("zlibDeflateFile: output handler is required.");
	}

	if (const auto whole_size = wholeBufferEligibleFileSize(path)) {
		const vBytes input = readWholeFile(path, *whole_size);
		libdeflateZlibCompress(
			std::span<const Byte>(input.data(), input.size()),
			libdeflateLevel(option, is_compressed_file),
			on_chunk);
		return;
	}

	std::ifstream file(path, std::ios::binary);
	if (!file) {
		throw std::runtime_error(std::format("Failed to open file: {}", path.string()));
	}

	auto input_buffer = std::make_unique_for_overwrite<Byte[]>(ZLIB_BUFSIZE);
	bool reached_eof = false;

	deflateDriver(option, is_compressed_file, on_chunk, [&](z_stream& strm) {
		if (strm.avail_in == 0 && !reached_eof) {
			file.read(
				reinterpret_cast<char*>(input_buffer.get()),
				static_cast<std::streamsize>(ZLIB_BUFSIZE)
			);
			const std::streamsize bytes_read = file.gcount();
			if (bytes_read == 0) {
				if (!file.eof()) {
					throw std::runtime_error("Failed to read full file: partial read");
				}
				reached_eof = true;
			} else {
				strm.next_in = input_buffer.get();
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
		appendBytes(result, std::span<const Byte>(buf, len));
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
