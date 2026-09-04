#include "compression.h"
#include "io_utils.h"

#include <libdeflate.h>
#include <unistd.h>
#include <zlib.h>

// STORED_LEVELS below deflates at level 0, which libdeflate only accepts from
// 1.8 onward -- older releases return NULL from libdeflate_alloc_compressor(0)
// and every stored payload fails at run time. CMakeLists.txt enforces the same
// floor at configure time; this covers builds that bypass it.
#if !defined(LIBDEFLATE_VERSION_MAJOR) || !defined(LIBDEFLATE_VERSION_MINOR)
#  error "pdvrdt requires libdeflate >= 1.8 (version macros not found in libdeflate.h)."
#elif LIBDEFLATE_VERSION_MAJOR < 1 || \
	(LIBDEFLATE_VERSION_MAJOR == 1 && LIBDEFLATE_VERSION_MINOR < 8)
#  error "pdvrdt requires libdeflate >= 1.8 (for compression level 0). Please upgrade libdeflate."
#endif

#include <algorithm>
#include <cerrno>
#include <format>
#include <limits>
#include <memory>
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

// Hand zlib the next uInt-sized slice of `input` once it has consumed the last
// one, advancing `offset`. Slicing matters on platforms where uInt is narrower
// than size_t; every driver below feeds its input through here.
void refillZlibInput(z_stream& strm, std::span<const Byte> input, std::size_t& offset) {
	if (strm.avail_in != 0 || offset >= input.size()) return;
	const std::size_t chunk_size = zlibChunkSize(input.size() - offset);
	strm.next_in = const_cast<Byte*>(input.data() + offset);
	strm.avail_in = static_cast<uInt>(chunk_size);
	offset += chunk_size;
}

[[nodiscard]] std::string zlibErrorMessage(const z_stream& strm, int ret) {
	return strm.msg ? std::string(strm.msg) : std::to_string(ret);
}

[[noreturn]] void throwZlibError(std::string_view operation, const z_stream& strm, int ret) {
	throw std::runtime_error(std::format("zlib {} failed: {}", operation, zlibErrorMessage(strm, ret)));
}

// Owns a z_stream's lifetime. The init/end halves must match, so they are picked
// together by one flag rather than by two near-identical classes. A failed init
// throws, so the destructor never runs on an uninitialised stream.
template <bool deflate_stream>
struct ZlibStreamGuard {
	z_stream strm{};

	explicit ZlibStreamGuard(int level = 0) {
		if constexpr (deflate_stream) {
			if (deflateInit(&strm, level) != Z_OK) throw std::runtime_error("zlib deflateInit failed.");
		} else {
			(void)level;
			if (inflateInit(&strm) != Z_OK) throw std::runtime_error("zlib inflateInit failed.");
		}
	}

	~ZlibStreamGuard() {
		if constexpr (deflate_stream) deflateEnd(&strm); else inflateEnd(&strm);
	}

	ZlibStreamGuard(const ZlibStreamGuard&) = delete;
	ZlibStreamGuard& operator=(const ZlibStreamGuard&) = delete;
};

using ZlibDeflateGuard = ZlibStreamGuard<true>;
using ZlibInflateGuard = ZlibStreamGuard<false>;

// Uninitialised scratch buffer that scrubs itself on scope exit. Holds secret or
// reversibly-compressed data, and make_unique_for_overwrite() skips the zero-fill
// a vector would pay on every call for a buffer that is about to be overwritten.
struct ScratchBuffer {
	std::unique_ptr<Byte[]> owned;
	std::span<Byte> bytes;
	ScopedWipe<std::span<Byte>> wipe{bytes};

	explicit ScratchBuffer(std::size_t size)
		: owned(std::make_unique_for_overwrite<Byte[]>(size)), bytes(owned.get(), size) {}

	[[nodiscard]] Byte* data() const noexcept { return bytes.data(); }
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
	ScratchBuffer buffer(ZLIB_BUFSIZE);
	std::size_t total_output = 0;
	std::size_t input_offset = 0;

	while (true) {
		refillZlibInput(strm, input, input_offset);
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

		if (ret == Z_STREAM_END) break;
		if (ret == Z_OK) {
			if (!made_progress) {
				throw std::runtime_error("zlib inflate failed: stalled stream.");
			}
			continue;
		}
		if (ret == Z_BUF_ERROR) {
			if (strm.avail_in == 0) {
				refillZlibInput(strm, input, input_offset);
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

// A compression level for each of the two back ends. Both emit a standard
// RFC 1950 zlib stream, so the recover-side inflate decodes either one and the
// pair must always describe the same intent.
//
// Z_DEFAULT_COMPRESSION (level 6), not Z_BEST_COMPRESSION (9): for the >64 MiB
// zlib streaming path, level 9 costs far more time for ~1-2% ratio. Measured on
// real corpora, libdeflate L9 costs ~2.6-2.9x the time of L6 for only ~1-2%
// smaller output (L10-12 are far worse). L6 is the ratio/time sweet spot, and it
// matches the level lodepng already re-encodes the PNG IDAT at (see
// lodepng_zlib_adapter::compress). Do NOT raise this back toward 9-12 without
// re-measuring.
//
// There is no stored counterpart here. Inputs that already live in a compressed
// container gain ~0% from a second deflate pass but cost real time, so they are
// stored instead of deflated -- and stored output is written directly by
// StoredZlibWriter below rather than asked of either back end. This holds in
// every mode: the Mastodon budget is the tightest one, but deflating a .zip/.mp4
// does not buy any of it back either.
struct DeflateLevels {
	int zlib;
	int libdeflate;
};

constexpr DeflateLevels DEFLATE_LEVELS { Z_DEFAULT_COMPRESSION, 6 };

// ------------------------------- stored output -------------------------------
//
// A deflate *stored* block applies no entropy coding, so once the block split is
// fixed the encoding is fully determined: the zlib header, then each block as a
// one-byte type field plus LEN and !LEN little-endian, then the Adler-32 of the
// input. Only the split is a choice, and the compressors disagree on it --
// libdeflate fills each block to the 65535-byte maximum, while zlib stops at
// 65531 and can add a trailing empty block, so the same bytes stored by zlib come
// out a few bytes longer.
//
// Writing the blocks here removes that choice from the back ends entirely. It
// keeps output identical to what libdeflate produced for inputs up to
// LIBDEFLATE_WHOLE_BUFFER_LIMIT, extends the same framing past that limit where
// zlib streaming used to take over with framing of its own, and gives the Rust
// port a specification it can match exactly rather than by coincidence.

// Largest run a single stored block can carry: its length is a 16-bit field.
constexpr std::size_t STORED_BLOCK_MAX_BYTES = 65535;

// RFC 1950 header for a stored stream: CMF 0x78 (deflate, 32 KiB window) and
// FLG 0x01 (FLEVEL 0, no preset dictionary, and 0x7801 % 31 == 0 as required).
constexpr std::array<Byte, 2> ZLIB_STORED_HEADER { 0x78, 0x01 };

// Stored output is handed on in pieces this size, not per block.
//
// The encryption layer turns each piece it is given into secretstream frames,
// splitting only at its own 1 MiB frame size, so the piece boundaries decide the
// profile's frame layout. libdeflate handed over one whole compressed buffer;
// passing on the small per-block pieces instead would frame the payload far more
// finely and inflate the profile by ~21 bytes per extra frame. Coalescing to a
// multiple of the frame size reproduces the previous frame layout exactly,
// without ever holding the payload whole.
constexpr std::size_t STORED_COALESCE_BYTES = ZLIB_BUFSIZE;

static_assert(STORED_COALESCE_BYTES % (1024 * 1024) == 0,
	"Stored output must be handed on in whole multiples of the encryption frame size, "
	"or the profile's frame layout changes.");

// Buffers writes into STORED_COALESCE_BYTES pieces before calling the handler.
class CoalescingSink {
public:
	explicit CoalescingSink(const DeflateChunkHandler& on_chunk)
		: on_chunk_(on_chunk), buffer_(STORED_COALESCE_BYTES) {}

	void write(std::span<const Byte> data) {
		while (!data.empty()) {
			const std::size_t take = std::min(STORED_COALESCE_BYTES - filled_, data.size());
			std::memcpy(buffer_.data() + filled_, data.data(), take);
			filled_ += take;
			data = data.subspan(take);
			if (filled_ == STORED_COALESCE_BYTES) {
				flush();
			}
		}
	}

	void flush() {
		if (filled_ != 0) {
			on_chunk_(std::span<const Byte>(buffer_.data(), filled_));
			filled_ = 0;
		}
	}

private:
	const DeflateChunkHandler& on_chunk_;
	ScratchBuffer buffer_;
	std::size_t filled_{0};
};

// Emits a stored RFC 1950 stream one block at a time, so a payload is never held
// whole: the callers below feed it a block at a time from memory or from a file.
class StoredZlibWriter {
public:
	explicit StoredZlibWriter(const DeflateChunkHandler& on_chunk) : sink_(on_chunk) {
		sink_.write(ZLIB_STORED_HEADER);
	}

	// `block` must be at most STORED_BLOCK_MAX_BYTES, and every block before the
	// final one must be exactly that size for the output to match libdeflate's.
	void writeBlock(std::span<const Byte> block, bool is_final) {
		[[assume(block.size() <= STORED_BLOCK_MAX_BYTES)]];
		const auto len = static_cast<std::uint16_t>(block.size());
		const std::array<Byte, 5> header {
			static_cast<Byte>(is_final ? 1 : 0),
			static_cast<Byte>(len & 0xFF),
			static_cast<Byte>(len >> 8),
			static_cast<Byte>(static_cast<std::uint16_t>(~len) & 0xFF),
			static_cast<Byte>(static_cast<std::uint16_t>(~len) >> 8)
		};
		sink_.write(header);

		if (!block.empty()) {
			sink_.write(block);
			adler_ = adler32(adler_, block.data(), static_cast<uInt>(block.size()));
		}
	}

	void finish() {
		// RFC 1950 stores the Adler-32 big-endian, unlike the little-endian block
		// lengths above.
		const auto sum = static_cast<std::uint32_t>(adler_);
		const std::array<Byte, 4> checksum {
			static_cast<Byte>(sum >> 24),
			static_cast<Byte>(sum >> 16),
			static_cast<Byte>(sum >> 8),
			static_cast<Byte>(sum)
		};
		sink_.write(checksum);
		sink_.flush();
	}

private:
	CoalescingSink sink_;
	uLong adler_{adler32(0, nullptr, 0)};
};

// The (offset, length, is_final) split a stored stream of `total` bytes uses.
// Always yields at least one block, so empty input still produces the single
// empty final block libdeflate emits for it.
template <typename Visit>
void forEachStoredBlock(std::size_t total, Visit&& visit) {
	std::size_t offset = 0;
	while (true) {
		const std::size_t remaining = total - offset;
		const std::size_t len = std::min(remaining, STORED_BLOCK_MAX_BYTES);
		const bool is_final = remaining <= STORED_BLOCK_MAX_BYTES;
		visit(offset, len, is_final);
		offset += len;
		if (is_final) return;
	}
}

// ----------------------------- libdeflate fast path -----------------------------
//
// Inputs at or below this size are deflated in a single whole-buffer libdeflate
// call (peak RSS ~= input + compressBound, ~2x input). Larger inputs fall back
// to the zlib streaming path, which holds only fixed-size chunks. Both paths
// emit a standard RFC 1950 zlib stream, so the recover-side zlib inflate
// decodes either one.
constexpr std::size_t LIBDEFLATE_WHOLE_BUFFER_LIMIT = 64ULL * 1024 * 1024;

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
	ScratchBuffer out(bound);

	const std::size_t produced = libdeflate_zlib_compress(
		compressor.c, input.data(), input.size(), out.data(), bound);
	if (produced == 0) {
		throw std::runtime_error("libdeflate: zlib compression failed.");
	}
	on_chunk(std::span<const Byte>(out.data(), produced));
}

[[nodiscard]] ssize_t preadRetry(int fd, Byte* buffer, std::size_t size, std::size_t offset) {
	if (offset > static_cast<std::size_t>(std::numeric_limits<off_t>::max())) {
		throw std::runtime_error("Failed to read input file: offset exceeds platform limit.");
	}
	while (true) {
		const ssize_t rc = ::pread(
			fd,
			buffer,
			size,
			static_cast<off_t>(offset)
		);
		if (rc < 0 && errno == EINTR) continue;
		return rc;
	}
}

[[noreturn]] void throwReadError() {
	const std::error_code ec(errno, std::generic_category());
	throw std::runtime_error(std::format("Failed to read input file: {}", ec.message()));
}

void verifyExpectedEof(int fd, std::size_t expected_size) {
	Byte extra{};
	const ssize_t rc = preadRetry(fd, &extra, 1, expected_size);
	if (rc < 0) throwReadError();
	if (rc != 0) {
		throw std::runtime_error("Failed to read file reliably: file grew while being read.");
	}
}

// Read an entire already-open regular file into memory at its validated size.
[[nodiscard]] vBytes readWholeFile(int fd, std::size_t size) {
	vBytes buffer(size);
	ScopedWipe buffer_wiper{buffer};
	std::size_t offset = 0;
	while (offset < size) {
		const std::size_t chunk_size = std::min<std::size_t>(
			size - offset,
			static_cast<std::size_t>(std::numeric_limits<ssize_t>::max())
		);
		const ssize_t rc = preadRetry(
			fd,
			buffer.data() + static_cast<std::ptrdiff_t>(offset),
			chunk_size,
			offset
		);
		if (rc < 0) throwReadError();
		if (rc == 0) throw std::runtime_error("Failed to read full file: partial read");
		offset += static_cast<std::size_t>(rc);
	}
	verifyExpectedEof(fd, size);
	buffer_wiper.release();
	return buffer;
}

// Stored (level 0) stream for a payload file, read one block at a time so the
// secret is never held whole in memory.
void storePayloadFd(int fd, std::size_t expected_size, const DeflateChunkHandler& on_chunk) {
	ScratchBuffer block(STORED_BLOCK_MAX_BYTES);
	StoredZlibWriter writer(on_chunk);

	forEachStoredBlock(expected_size, [&](std::size_t offset, std::size_t len, bool is_final) {
		std::size_t filled = 0;
		while (filled < len) {
			const ssize_t rc = preadRetry(
				fd,
				block.data() + static_cast<std::ptrdiff_t>(filled),
				len - filled,
				offset + filled
			);
			if (rc < 0) throwReadError();
			if (rc == 0) throw std::runtime_error("Failed to read full file: partial read");
			filled += static_cast<std::size_t>(rc);
		}
		writer.writeBlock(std::span<const Byte>(block.data(), len), is_final);
	});

	verifyExpectedEof(fd, expected_size);
	writer.finish();
}

template <typename RefillInput>
void deflateDriver(int level, const DeflateChunkHandler& on_chunk, RefillInput&& refill_input) {
	ZlibDeflateGuard guard(level);
	z_stream& strm = guard.strm;
	ScratchBuffer output_buffer(ZLIB_BUFSIZE);

	while (true) {
		const bool reached_eof = refill_input(strm);

		strm.next_out = output_buffer.data();
		strm.avail_out = static_cast<uInt>(ZLIB_BUFSIZE);

		const auto total_in_before = strm.total_in;
		const auto total_out_before = strm.total_out;
		const int ret = deflate(&strm, reached_eof ? Z_FINISH : Z_NO_FLUSH);
		const std::size_t written = ZLIB_BUFSIZE - strm.avail_out;
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
			throwZlibError("deflate", strm, ret);
		}
	}
}

} // namespace

void zlibStoreSpan(std::span<const Byte> data, const DeflateChunkHandler& on_chunk) {
	if (!on_chunk) {
		throw std::invalid_argument("zlibStoreSpan: output handler is required.");
	}

	StoredZlibWriter writer(on_chunk);
	forEachStoredBlock(data.size(), [&](std::size_t offset, std::size_t len, bool is_final) {
		writer.writeBlock(data.subspan(offset, len), is_final);
	});
	writer.finish();
}

void zlibDeflateFd(int fd, std::size_t expected_size, bool is_compressed_file, const DeflateChunkHandler& on_chunk) {
	if (!on_chunk) {
		throw std::invalid_argument("zlibDeflateFd: output handler is required.");
	}
	if (fd < 0) {
		throw std::invalid_argument("zlibDeflateFd: valid input descriptor is required.");
	}

	// An already-compressed payload is stored rather than deflated. Stored output
	// is written directly at every size, so it needs neither back end and never
	// reads the payload whole -- one block at a time is enough.
	if (is_compressed_file) {
		storePayloadFd(fd, expected_size, on_chunk);
		return;
	}

	if (expected_size <= LIBDEFLATE_WHOLE_BUFFER_LIMIT) {
		vBytes input = readWholeFile(fd, expected_size);
		ScopedWipe input_wiper{input};
		libdeflateZlibCompress(
			std::span<const Byte>(input.data(), input.size()),
			DEFLATE_LEVELS.libdeflate,
			on_chunk);
		return;
	}

	ScratchBuffer input_buffer(ZLIB_BUFSIZE);
	bool reached_eof = false;
	std::size_t input_offset = 0;

	deflateDriver(DEFLATE_LEVELS.zlib, on_chunk, [&](z_stream& strm) {
		if (strm.avail_in == 0 && !reached_eof) {
			if (input_offset == expected_size) {
				verifyExpectedEof(fd, expected_size);
				reached_eof = true;
			} else {
				const std::size_t request_size = std::min(ZLIB_BUFSIZE, expected_size - input_offset);
				const ssize_t rc = preadRetry(fd, input_buffer.data(), request_size, input_offset);
				if (rc < 0) throwReadError();
				if (rc == 0) throw std::runtime_error("Failed to read full file: partial read");
				strm.next_in = input_buffer.data();
				strm.avail_in = static_cast<uInt>(rc);
				input_offset += static_cast<std::size_t>(rc);
				if (input_offset == expected_size) {
					verifyExpectedEof(fd, expected_size);
					reached_eof = true;
				}
			}
		}
		return reached_eof && strm.avail_in == 0;
	});
}

vBytes zlibInflatePrefix(std::span<const Byte> data, std::size_t prefix_size) {
	if (prefix_size == 0) return {};
	if (prefix_size > static_cast<std::size_t>(std::numeric_limits<uInt>::max())) {
		throw std::invalid_argument("zlibInflatePrefix: prefix is too large.");
	}

	ZlibInflateGuard guard;
	z_stream& strm = guard.strm;
	vBytes prefix(prefix_size);
	strm.next_out = prefix.data();
	strm.avail_out = static_cast<uInt>(prefix.size());
	std::size_t input_offset = 0;

	while (strm.avail_out != 0) {
		refillZlibInput(strm, data, input_offset);
		const auto total_in_before = strm.total_in;
		const auto total_out_before = strm.total_out;
		const int ret = inflate(&strm, Z_NO_FLUSH);
		const bool made_progress =
			strm.total_in != total_in_before || strm.total_out != total_out_before;

		if (ret == Z_STREAM_END) {
			prefix.resize(static_cast<std::size_t>(strm.total_out));
			return prefix;
		}
		if (ret == Z_OK && made_progress) continue;
		if (ret == Z_BUF_ERROR && strm.avail_in == 0) {
			refillZlibInput(strm, data, input_offset);
			if (strm.avail_in != 0) continue;
		}
		if (ret != Z_OK && ret != Z_BUF_ERROR) {
			throwZlibError("inflate prefix", strm, ret);
		}
		throw std::runtime_error("zlib inflate prefix failed: truncated or stalled stream.");
	}

	return prefix;
}

vBytes zlibInflateSpanBounded(std::span<const Byte> data, std::size_t max_output_size) {
	vBytes result;
	result.reserve(inflateReserveHint(data.size(), max_output_size));
	inflateDriver(data, max_output_size, [&](const Byte* buf, std::size_t len) {
		appendBytes(result, std::span<const Byte>(buf, len),
			"Zlib Compression Error: Inflated output size overflow.");
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
