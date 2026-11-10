#include "compression.h"
#include "io_utils.h"
#include "parallel.h"

#include <libdeflate.h>
#include <unistd.h>
#include <zlib.h>

// libdeflate floor. The 1.8 minimum was originally set by level-0 (stored)
// compression; stored streams are now written by StoredZlibWriter below, so that
// reason is gone. The floor is kept because 1.8 is the oldest release pdvrdt has
// been checked against -- every libdeflate call it makes (level-6 compressors,
// zlib_compress_bound, zlib_decompress_ex, adler32, crc32) is older still, but
// lowering it needs a build and test run against that older release first.
// CMakeLists.txt enforces the same floor at configure time; this covers builds
// that bypass it.
#if !defined(LIBDEFLATE_VERSION_MAJOR) || !defined(LIBDEFLATE_VERSION_MINOR)
#  error "pdvrdt requires libdeflate >= 1.8 (version macros not found in libdeflate.h)."
#elif LIBDEFLATE_VERSION_MAJOR < 1 || \
	(LIBDEFLATE_VERSION_MAJOR == 1 && LIBDEFLATE_VERSION_MINOR < 8)
#  error "pdvrdt requires libdeflate >= 1.8. Please upgrade libdeflate."
#endif

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <format>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

constexpr std::size_t ZLIB_BUFSIZE = 2 * 1024 * 1024;
constexpr std::size_t MAX_INFLATED_OUTPUT_SIZE = 3uz * 1024 * 1024 * 1024;
constexpr std::size_t MIN_INFLATE_INITIAL_RESERVE = 256 * 1024;
constexpr std::size_t MAX_INFLATE_INITIAL_RESERVE = 64uz * 1024 * 1024;

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

// zlib allocation callbacks that scrub every block before it is freed.
//
// zlib's internal state holds copies of the data streaming through it -- the
// deflate window and pending buffer, the inflate window -- and deflateEnd() /
// inflateEnd() release that memory as-is under the default allocator. zfree is
// not told the block's size, so each allocation carries it in a header sized to
// keep the returned pointer max-aligned, as malloc's would be.
constexpr std::size_t ZALLOC_HEADER_BYTES = alignof(std::max_align_t);
static_assert(ZALLOC_HEADER_BYTES >= sizeof(std::size_t));

voidpf wipingZalloc(voidpf /*opaque*/, uInt items, uInt size) {
	const std::size_t count = items, each = size;
	if (each != 0 && count > (std::numeric_limits<std::size_t>::max() - ZALLOC_HEADER_BYTES) / each) {
		return Z_NULL;
	}
	const std::size_t bytes = count * each;
	auto* block = static_cast<Byte*>(std::calloc(1, ZALLOC_HEADER_BYTES + bytes));
	if (block == nullptr) return Z_NULL;
	std::memcpy(block, &bytes, sizeof(bytes));
	return block + ZALLOC_HEADER_BYTES;
}

void wipingZfree(voidpf /*opaque*/, voidpf address) {
	if (address == Z_NULL) return;
	Byte* const block = static_cast<Byte*>(address) - ZALLOC_HEADER_BYTES;
	std::size_t bytes{};
	std::memcpy(&bytes, block, sizeof(bytes));
	sodium_memzero(block, ZALLOC_HEADER_BYTES + bytes);
	std::free(block);
}

// Owns a z_stream's lifetime. The init/end halves must match, so they are picked
// together by one flag rather than by two near-identical classes. A failed init
// throws, so the destructor never runs on an uninitialised stream.
template <bool deflate_stream>
struct ZlibStreamGuard {
	z_stream strm{};

	explicit ZlibStreamGuard(int level = 0) {
		strm.zalloc = wipingZalloc;
		strm.zfree = wipingZfree;
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

// A zlib stream inflated as it arrives, in pieces of any size, with each run
// of output handed to `on_output` under a ceiling on the total. Bytes after
// the end of the stream are refused, whether in the piece that ends it or a
// later one, and finish() refuses a stream that never ended. Whole-buffer
// inflation is one feed() and finish(). (zlibInflatePrefix(), which stops
// short of the end on purpose, is separate.)
class IncrementalInflater {
public:
	using OutputSink = std::function<void(std::span<const Byte>)>;

	IncrementalInflater(std::size_t max_output_size, OutputSink on_output)
		: max_output_size_(max_output_size), on_output_(std::move(on_output)) {}

	void feed(std::span<const Byte> input) {
		z_stream& strm = guard_.strm;
		std::size_t offset = 0;
		// A full output buffer means zlib may hold more output than it returned,
		// so inflate is called again even once this piece is consumed.
		bool output_full = false;
		while (true) {
			refillZlibInput(strm, input, offset);
			if (strm.avail_in == 0 && !output_full) {
				return;
			}
			if (ended_) {
				throw std::runtime_error("zlib inflate failed: trailing data after stream end.");
			}
			strm.next_out = buffer_.data();
			strm.avail_out = static_cast<uInt>(ZLIB_BUFSIZE);
			const auto total_in_before = strm.total_in;
			const auto total_out_before = strm.total_out;
			const int ret = inflate(&strm, Z_NO_FLUSH);
			const std::size_t produced = ZLIB_BUFSIZE - strm.avail_out;
			if (produced > 0) {
				if (produced > max_output_size_ || total_output_ > max_output_size_ - produced) {
					throw std::runtime_error("Zlib Compression Error: Inflated data exceeds maximum program size limit.");
				}
				on_output_(std::span<const Byte>(buffer_.data(), produced));
				total_output_ += produced;
			}
			output_full = strm.avail_out == 0;
			const bool made_progress =
				strm.total_in != total_in_before || strm.total_out != total_out_before;

			if (ret == Z_STREAM_END) {
				ended_ = true;
				if (strm.avail_in != 0 || offset < input.size()) {
					throw std::runtime_error("zlib inflate failed: trailing data after stream end.");
				}
				return;
			}
			if (ret == Z_OK) {
				if (!made_progress) {
					throw std::runtime_error("zlib inflate failed: stalled stream.");
				}
				continue;
			}
			if (ret == Z_BUF_ERROR) {
				// No progress possible: either this piece is used up and the
				// stream needs the next one, or it is stuck.
				if (strm.avail_in == 0 && offset >= input.size()) {
					return;
				}
				throw std::runtime_error("zlib inflate failed: stalled stream.");
			}
			throwZlibError("inflate", strm, ret);
		}
	}

	// Total bytes produced; throws unless the stream ended.
	[[nodiscard]] std::size_t finish() const {
		if (!ended_) {
			throw std::runtime_error("zlib inflate failed: stalled stream.");
		}
		return total_output_;
	}

private:
	ZlibInflateGuard guard_;
	ScratchBuffer buffer_{ZLIB_BUFSIZE};
	std::size_t max_output_size_;
	OutputSink on_output_;
	std::size_t total_output_{0};
	bool ended_{false};
};

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
		// A real check, not [[assume]]: an oversized block would otherwise be
		// silently truncated by the 16-bit LEN field below.
		if (block.size() > STORED_BLOCK_MAX_BYTES) {
			throw std::invalid_argument("StoredZlibWriter: stored block exceeds 65535 bytes.");
		}
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
			adler_ = libdeflate_adler32(adler_, block.data(), block.size());
		}
	}

	void finish() {
		// RFC 1950 stores the Adler-32 big-endian, unlike the little-endian block
		// lengths above.
		const std::uint32_t sum = adler_;
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
	std::uint32_t adler_{1};  // Adler-32 initial value (RFC 1950)
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
constexpr std::size_t LIBDEFLATE_WHOLE_BUFFER_LIMIT = 64uz * 1024 * 1024;

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

void verifyExpectedEof(int fd, std::size_t expected_size) {
	Byte extra{};
	const PreadResult result = preadFill(fd, std::span<Byte>(&extra, 1), expected_size);
	if (result.error != 0) {
		const std::error_code ec(result.error, std::generic_category());
		throw std::runtime_error(std::format("Failed to read input file: {}", ec.message()));
	}
	if (result.filled != 0) {
		throw std::runtime_error("Failed to read file reliably: file grew while being read.");
	}
}

// Read an entire already-open regular file into memory at its validated size.
[[nodiscard]] SecureBytes readWholeFile(int fd, std::size_t size) {
	SecureBytes buffer(size);
	preadExact(fd, buffer.span(), 0);
	verifyExpectedEof(fd, size);
	return buffer;
}

// Stored (level 0) stream for a payload file, read one block at a time so the
// secret is never held whole in memory.
void storePayloadFd(int fd, std::size_t expected_size, const DeflateChunkHandler& on_chunk) {
	ScratchBuffer block(STORED_BLOCK_MAX_BYTES);
	StoredZlibWriter writer(on_chunk);

	forEachStoredBlock(expected_size, [&](std::size_t offset, std::size_t len, bool is_final) {
		preadExact(fd, block.bytes.first(len), offset);
		writer.writeBlock(block.bytes.first(len), is_final);
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

// ------------------------------ parallel deflate ------------------------------
//
// Above LIBDEFLATE_WHOLE_BUFFER_LIMIT a payload used to go through one zlib
// stream, ~35 MB/s on a single core: about a minute for the 2 GiB maximum.
// Instead the payload is read a window at a time and its fixed 1 MiB slices
// are deflated in parallel, pigz-style: each slice is primed with the 32 KiB
// before it as its dictionary, so matches still reach back across the seam,
// and ended with a sync flush (an empty stored block), so the slices
// concatenate into one valid deflate stream; their Adler-32s are combined.
// Same level, ~1% larger output, and the bytes depend only on the slice size,
// never on the thread count. Memory stays bounded by the window, and every
// buffer that holds payload bytes -- the window, the slice outputs and zlib's
// own state -- is wiped before it is freed.

constexpr std::size_t PARALLEL_SLICE_BYTES = 1024uz * 1024;
constexpr std::size_t DEFLATE_DICTIONARY_BYTES = 32uz * 1024;
// One stream is all a single core can run, so parallel deflate needs two.
constexpr unsigned MIN_PARALLEL_DEFLATE_THREADS = 2;

// RFC 1950 header for a deflate stream at the default level: CMF 0x78
// (deflate, 32 KiB window), FLG 0x9C (FLEVEL 2, no preset dictionary).
constexpr std::array<Byte, 2> ZLIB_DEFAULT_HEADER { 0x78, 0x9C };

void parallelDeflateFd(int fd, std::size_t expected_size, int level, unsigned threads,
	const DeflateChunkHandler& on_chunk) {
	// Two slices per thread per window: as fast as more (measured), and the
	// window plus its outputs stay ~4 MiB per thread. The 32 KiB in front of
	// the window carries the previous window's tail, the first slice's
	// dictionary.
	const std::size_t window_slices = static_cast<std::size_t>(threads) * 2U;
	const std::size_t window_bytes = window_slices * PARALLEL_SLICE_BYTES;
	ScratchBuffer input(DEFLATE_DICTIONARY_BYTES + window_bytes);
	Byte* const window = input.data() + DEFLATE_DICTIONARY_BYTES;
	std::vector<std::unique_ptr<ScratchBuffer>> outputs(window_slices);
	std::vector<std::size_t> produced(window_slices);
	std::vector<uLong> slice_adler(window_slices);

	CoalescingSink sink(on_chunk);
	sink.write(ZLIB_DEFAULT_HEADER);
	uLong adler = 1;
	std::size_t dictionary = 0;  // bytes of real data in front of `window`

	for (std::size_t offset = 0; offset < expected_size; offset += window_bytes) {
		const std::size_t length = std::min(window_bytes, expected_size - offset);
		preadExact(fd, std::span<Byte>(window, length), offset);
		const bool last_window = offset + length == expected_size;
		if (last_window) {
			verifyExpectedEof(fd, expected_size);
		}
		const std::size_t slices = (length + PARALLEL_SLICE_BYTES - 1U) / PARALLEL_SLICE_BYTES;

		std::atomic<std::size_t> next{0};
		parallelFor(std::min<std::size_t>(slices, threads), 1, threads, [&](std::size_t, std::size_t) {
			z_stream strm{};
			strm.zalloc = wipingZalloc;
			strm.zfree = wipingZfree;
			if (deflateInit2(&strm, level, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
				throw std::runtime_error("zlib deflateInit failed.");
			}
			struct End {
				z_stream& strm;
				~End() { deflateEnd(&strm); }
			} end{strm};

			for (std::size_t index; (index = next.fetch_add(1)) < slices;) {
				const std::size_t start = index * PARALLEL_SLICE_BYTES;
				const std::size_t slice_length = std::min(PARALLEL_SLICE_BYTES, length - start);
				const bool final_slice = last_window && index + 1U == slices;
				if (deflateReset(&strm) != Z_OK) {
					throw std::runtime_error("zlib deflateReset failed.");
				}
				const std::size_t primed = index == 0 ? dictionary : DEFLATE_DICTIONARY_BYTES;
				if (primed != 0 &&
					deflateSetDictionary(&strm, window + start - primed, static_cast<uInt>(primed)) != Z_OK) {
					throw std::runtime_error("zlib deflateSetDictionary failed.");
				}
				// deflateBound plus the sync flush's empty stored block.
				const std::size_t bound = deflateBound(&strm, static_cast<uLong>(slice_length)) + 16U;
				if (!outputs[index] || outputs[index]->bytes.size() < bound) {
					outputs[index] = std::make_unique<ScratchBuffer>(bound);
				}
				strm.next_in = window + start;
				strm.avail_in = static_cast<uInt>(slice_length);
				strm.next_out = outputs[index]->data();
				strm.avail_out = static_cast<uInt>(outputs[index]->bytes.size());
				const int ret = deflate(&strm, final_slice ? Z_FINISH : Z_SYNC_FLUSH);
				if ((final_slice ? ret != Z_STREAM_END : ret != Z_OK) || strm.avail_in != 0) {
					throwZlibError("deflate", strm, ret);
				}
				produced[index] = outputs[index]->bytes.size() - strm.avail_out;
				slice_adler[index] = adler32(1, window + start, static_cast<uInt>(slice_length));
			}
		});

		for (std::size_t index = 0; index < slices; ++index) {
			sink.write(std::span<const Byte>(outputs[index]->data(), produced[index]));
			sodium_memzero(outputs[index]->data(), produced[index]);
			const std::size_t slice_length = std::min(PARALLEL_SLICE_BYTES, length - index * PARALLEL_SLICE_BYTES);
			adler = adler32_combine(adler, slice_adler[index], static_cast<z_off_t>(slice_length));
		}

		// The window's tail primes the next window's first slice.
		const std::size_t keep = std::min(DEFLATE_DICTIONARY_BYTES, dictionary + length);
		std::memmove(window - keep, window + length - keep, keep);
		dictionary = keep;
	}

	const std::array<Byte, 4> checksum {
		static_cast<Byte>(adler >> 24), static_cast<Byte>(adler >> 16),
		static_cast<Byte>(adler >> 8), static_cast<Byte>(adler)
	};
	sink.write(checksum);
	sink.flush();
}

} // namespace

std::size_t zlibStoredStreamSize(std::size_t input_size) {
	constexpr const char* OVERFLOW_ERROR = "Zlib Compression Error: Stored stream size overflow.";
	constexpr std::size_t BLOCK_HEADER_BYTES = 5, ADLER32_BYTES = 4;
	// forEachStoredBlock() always emits at least one block, even for empty input.
	const std::size_t blocks = input_size == 0
		? 1
		: 1 + (input_size - 1) / STORED_BLOCK_MAX_BYTES;
	std::size_t total = checkedMulSize(blocks, BLOCK_HEADER_BYTES, OVERFLOW_ERROR);
	total = checkedAddSize(total, input_size, OVERFLOW_ERROR);
	return checkedAddSize(total, ZLIB_STORED_HEADER.size() + ADLER32_BYTES, OVERFLOW_ERROR);
}

std::size_t zlibPayloadStreamBound(std::size_t input_size, bool is_compressed_file) {
	const std::size_t stored = zlibStoredStreamSize(input_size);
	if (is_compressed_file) return stored;
	constexpr const char* OVERFLOW_ERROR = "Zlib Compression Error: Deflate bound overflow.";
	constexpr std::size_t FIXED_SLACK = 1024uz * 1024;
	return std::max(stored, checkedAddSize(
		checkedAddSize(input_size, input_size / 1024, OVERFLOW_ERROR), FIXED_SLACK, OVERFLOW_ERROR));
}

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
		const SecureBytes input = readWholeFile(fd, expected_size);
		libdeflateZlibCompress(
			input.span(),
			DEFLATE_LEVELS.libdeflate,
			on_chunk);
		return;
	}

	if (const unsigned threads = workerCount(); threads >= MIN_PARALLEL_DEFLATE_THREADS) {
		parallelDeflateFd(fd, expected_size, DEFLATE_LEVELS.zlib, threads, on_chunk);
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
				preadExact(fd, input_buffer.bytes.first(request_size), input_offset);
				strm.next_in = input_buffer.data();
				strm.avail_in = static_cast<uInt>(request_size);
				input_offset += request_size;
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
	IncrementalInflater inflater(max_output_size, [&](std::span<const Byte> bytes) {
		appendBytes(result, bytes, "Zlib Compression Error: Inflated output size overflow.");
	});
	inflater.feed(data);
	(void)inflater.finish();
	return result;
}

struct ZlibInflateToFd::State {
	IncrementalInflater inflater;

	explicit State(int fd)
		: inflater(MAX_INFLATED_OUTPUT_SIZE, [fd](std::span<const Byte> bytes) { writeAllToFd(fd, bytes); }) {}
};

ZlibInflateToFd::ZlibInflateToFd(int fd) : state_(std::make_unique<State>(fd)) {}
ZlibInflateToFd::~ZlibInflateToFd() = default;

void ZlibInflateToFd::feed(std::span<const Byte> input) {
	state_->inflater.feed(input);
}

std::size_t ZlibInflateToFd::finish() {
	const std::size_t total_output = state_->inflater.finish();
	if (total_output == 0) {
		throw std::runtime_error("Zlib Compression Error: Output file is empty. Inflating file failed.");
	}
	return total_output;
}

std::size_t zlibInflateToFd(const vBytes& data_vec, int fd) {
	ZlibInflateToFd inflater(fd);
	inflater.feed(data_vec);
	return inflater.finish();
}
