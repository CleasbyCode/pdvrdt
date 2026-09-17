// Keep the PNG fixtures independent of LodePNG's encoder and size calculations.
// Linker wrappers check which decompressor ran without timing-sensitive assertions.
#include "lodepng/lodepng_zlib_adapter.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<unsigned char>;
using Decoder = unsigned (*)(unsigned char**, size_t*, const unsigned char*, size_t,
	const LodePNGDecompressSettings*);
constexpr unsigned OUTPUT_LIMIT_ERROR = 109;
unsigned streaming_calls = 0;
bool force_streaming = false;

void requireTest(bool condition, std::string_view message) {
	if (!condition) throw std::runtime_error(std::string(message));
}

Bytes zlibStream(const Bytes& raw) {
	uLongf size = compressBound(static_cast<uLong>(raw.size()));
	Bytes compressed(size);
	requireTest(compress2(compressed.data(), &size, raw.data(),
		static_cast<uLong>(raw.size()), Z_BEST_COMPRESSION) == Z_OK,
		"Unable to create zlib fixture.");
	compressed.resize(size);
	return compressed;
}

void append32(Bytes& bytes, std::uint32_t value) {
	for (unsigned shift : {24U, 16U, 8U, 0U}) {
		bytes.push_back(static_cast<unsigned char>((value >> shift) & 0xffU));
	}
}

void appendChunk(Bytes& png, const std::array<unsigned char, 4>& type, const Bytes& data) {
	append32(png, static_cast<std::uint32_t>(data.size()));
	const size_t start = png.size();
	png.insert(png.end(), type.begin(), type.end());
	png.insert(png.end(), data.begin(), data.end());
	append32(png, static_cast<std::uint32_t>(crc32(0, png.data() + start,
		static_cast<uInt>(data.size() + type.size()))));
}

Bytes makePng(unsigned width, unsigned height, unsigned depth, unsigned color,
	unsigned interlace, const Bytes& scanlines) {
	Bytes png{137, 80, 78, 71, 13, 10, 26, 10};
	Bytes ihdr;
	append32(ihdr, width);
	append32(ihdr, height);
	ihdr.insert(ihdr.end(), {static_cast<unsigned char>(depth),
		static_cast<unsigned char>(color), 0, 0, static_cast<unsigned char>(interlace)});
	appendChunk(png, {'I', 'H', 'D', 'R'}, ihdr);
	appendChunk(png, {'I', 'D', 'A', 'T'}, zlibStream(scanlines));
	appendChunk(png, {'I', 'E', 'N', 'D'}, {});
	return png;
}

struct Capture {
	unsigned calls = 0;
	LodePNGDecompressSettings observed{};
};

unsigned unusedInflate(unsigned char**, size_t*, const unsigned char*, size_t,
	const LodePNGDecompressSettings*) {
	throw std::runtime_error("The unused custom_inflate callback was invoked.");
}

unsigned captureDecode(unsigned char** out, size_t* outsize,
	const unsigned char* in, size_t insize, const LodePNGDecompressSettings* settings) {
	auto& capture = *static_cast<Capture*>(const_cast<void*>(settings->custom_context));
	++capture.calls;
	capture.observed = *settings;
	return lodepng_zlib_adapter::decompress(out, outsize, in, insize, settings);
}

struct PngResult {
	unsigned error;
	unsigned stream_calls;
	Bytes pixels;
};

PngResult decodePng(const Bytes& png, size_t requested_limit, size_t expected_limit) {
	lodepng::State state;
	Capture capture;
	auto& settings = state.decoder.zlibsettings;
	settings.max_output_size = requested_limit;
	settings.custom_zlib = captureDecode;
	settings.custom_inflate = unusedInflate;
	settings.custom_context = &capture;
	// Distinct nondefault values verify that narrowing the bound preserves the
	// caller's other settings. The fixtures themselves have valid checksums.
	settings.ignore_adler32 = 1;
	settings.ignore_nlen = 1;
	streaming_calls = 0;
	PngResult result{};
	unsigned width = 0;
	unsigned height = 0;
	result.error = lodepng::decode(result.pixels, width, height, state, png);
	result.stream_calls = streaming_calls;
	requireTest(capture.calls == 1, "PNG decoding did not invoke the custom zlib callback once.");
	requireTest(capture.observed.max_output_size == expected_limit,
		"PNG callback did not receive the expected scanline bound.");
	requireTest(capture.observed.custom_context == &capture &&
		capture.observed.custom_zlib == captureDecode &&
		capture.observed.custom_inflate == unusedInflate &&
		capture.observed.ignore_adler32 == 1 && capture.observed.ignore_nlen == 1,
		"Narrowing the callback limit changed unrelated custom settings.");
	requireTest(settings.max_output_size == requested_limit &&
		settings.custom_context == &capture && settings.custom_zlib == captureDecode &&
		settings.custom_inflate == unusedInflate &&
		settings.ignore_adler32 == 1 && settings.ignore_nlen == 1,
		"PNG decoding mutated the caller's decompression settings.");
	return result;
}

void checkGeometryBounds() {
	struct Fixture {
		unsigned width, height, depth, color, interlace;
		size_t filtered_size;
	};
	// Hand-calculated filtered lengths, including filter bytes for nonempty
	// Adam7 passes. For 5x5 grey1 these are 2+2+2+4+2+6+4; for 9x7 RGBA8,
	// 9+5+13+18+42+68+111. Empty passes contribute no filter byte.
	constexpr Fixture fixtures[] = {
		{1, 1, 8, 6, 0, 5},
		{13, 3, 1, 0, 0, 9},
		{1, 1, 1, 0, 1, 2},
		{1, 1, 8, 6, 1, 5},
		{5, 5, 1, 0, 1, 22},
		{9, 7, 8, 6, 1, 266},
		{3, 2, 16, 2, 0, 38}
	};
	for (const auto& fixture : fixtures) {
		const Bytes png = makePng(fixture.width, fixture.height, fixture.depth,
			fixture.color, fixture.interlace, Bytes(fixture.filtered_size, 0));
		const auto result = decodePng(png, 0, fixture.filtered_size);
		requireTest(result.error == 0, "Valid PNG failed at its exact scanline bound.");
		requireTest(result.stream_calls == 0, "Known-size PNG unnecessarily used streaming inflate.");
		Bytes expected(static_cast<size_t>(fixture.width) * fixture.height * 4U, 0);
		if (fixture.color != 6) {
			for (size_t i = 3; i < expected.size(); i += 4) expected[i] = 255;
		}
		requireTest(result.pixels == expected, "Bounded PNG decoding changed the decoded pixels.");
	}
	std::cout << "[PASS] exact IHDR bounds and decoded pixels for packed, 16-bit, and Adam7 PNGs\n";
}

void checkPngRejectionAndSettings() {
	const Bytes valid = makePng(1, 1, 8, 6, 0, Bytes(5, 0));
	requireTest(decodePng(valid, 100, 5).error == 0, "Loose caller bound prevented valid PNG decoding.");
	requireTest(decodePng(valid, 5, 5).error == 0, "Exact caller bound prevented valid PNG decoding.");
	requireTest(decodePng(valid, 4, 4).error == OUTPUT_LIMIT_ERROR,
		"PNG decoding discarded a stricter caller bound.");
	const auto short_output = decodePng(makePng(1, 1, 8, 6, 0, Bytes(4, 0)), 0, 5);
	requireTest(short_output.error == 91, "PNG decoding lost its exact scanline size check.");

	// This stream expands roughly 1,000 times; the declared image needs five
	// bytes. Checking the bound and absence of fallback detects early rejection
	// without a large allocation or a machine-dependent timing threshold.
	const Bytes oversized = makePng(1, 1, 8, 6, 0, Bytes(4U * 1024U * 1024U, 0));
	const auto fast_result = decodePng(oversized, 0, 5);
	requireTest(fast_result.error == OUTPUT_LIMIT_ERROR && fast_result.stream_calls == 0,
		"Oversized IDAT expanded through streaming instead of failing at its PNG bound.");
	force_streaming = true;
	const auto streaming_valid = decodePng(valid, 0, 5);
	const auto streaming_large = decodePng(oversized, 0, 5);
	force_streaming = false;
	requireTest(streaming_valid.error == 0 && streaming_valid.stream_calls == 1,
		"Streaming fallback rejected an exact-size PNG.");
	requireTest(streaming_large.error == OUTPUT_LIMIT_ERROR && streaming_large.stream_calls == 1,
		"Streaming fallback failed to enforce the PNG scanline bound.");
	std::cout << "[PASS] tiny-IHDR expansion rejected in both paths; stricter settings and size checks retained\n";
}

void checkHighCompressionFastPath() {
	constexpr size_t filtered_size = 512U * (1U + 512U * 4U);
	const Bytes scanlines(filtered_size, 0);
	const Bytes png = makePng(512, 512, 8, 6, 0, scanlines);
	requireTest(png.size() * 4U + 1024U < filtered_size,
		"High-compression fixture would fit the old compressed-size estimate.");
	const auto result = decodePng(png, 0, filtered_size);
	requireTest(result.error == 0 && result.stream_calls == 0,
		"Highly compressed valid PNG was not decoded in one libdeflate pass.");
	requireTest(result.pixels == Bytes(512U * 512U * 4U, 0), "High-compression PNG pixels changed.");
	std::cout << "[PASS] highly compressed valid IDAT stays on the libdeflate fast path\n";
}

void checkPaletteValidation() {
	const auto check = [](unsigned depth, unsigned color, size_t palette_bytes, unsigned expected_error) {
		const bool indexed = color == 3;
		const Bytes expected = indexed ? Bytes{0x46, 0x46, 0x46, 255}
			: Bytes{0x29, 0x53, 0x7f, static_cast<unsigned char>(color == 6 ? 0xad : 255)};
		Bytes scanline{0};
		if (indexed) {
			scanline.push_back(0);
		} else {
			for (unsigned channel = 0; channel < (color == 6 ? 4U : 3U); ++channel) {
				scanline.push_back(expected[channel]);
				if (depth == 16) scanline.push_back(0x19);
			}
		}
		Bytes png = makePng(1, 1, depth, color, 0, scanline);
		Bytes chunk;
		appendChunk(chunk, {'P', 'L', 'T', 'E'}, Bytes(palette_bytes, 0x46));
		png.insert(png.begin() + 33, chunk.begin(), chunk.end());
		const std::string label = "PLTE bytes " + std::to_string(palette_bytes) +
			", color " + std::to_string(color) + ", depth " + std::to_string(depth);

		lodepng::State inspection;
		unsigned width = 0, height = 0;
		requireTest(lodepng_inspect(&width, &height, &inspection, png.data(), png.size()) == 0,
			label + ": fixture has an invalid IHDR.");
		requireTest(lodepng_inspect_chunk(&inspection, 33, png.data(), png.size()) == expected_error,
			label + ": chunk inspection returned the wrong result.");
		lodepng::State decoding;
		lodepng_zlib_adapter::configureDecoder(decoding);
		Bytes pixels;
		requireTest(lodepng::decode(pixels, width, height, decoding, png) == expected_error,
			label + ": full decoding returned the wrong result.");
		if (expected_error == 0) {
			requireTest(inspection.info_png.color.palettesize == palette_bytes / 3 &&
				decoding.info_png.color.palettesize == palette_bytes / 3,
				label + ": palette entry count changed.");
			requireTest(width == 1 && height == 1 && pixels == expected,
				label + ": decoded pixels changed.");
		} else {
			requireTest(inspection.info_png.color.palette == nullptr &&
				inspection.info_png.color.palettesize == 0 && pixels.empty(),
				label + ": invalid palette was accepted or exposed pixels.");
		}
	};
	for (size_t length : {0U, 1U, 2U, 4U, 5U, 767U, 769U, 770U, 771U}) {
		check(8, 3, length, 38);
	}
	for (unsigned depth : {1U, 2U, 4U, 8U}) {
		const size_t max_entries = size_t{1} << depth;
		check(depth, 3, 3, 0); // Fewer entries than the index range is valid.
		check(depth, 3, max_entries * 3, 0);
		check(depth, 3, (max_entries + 1) * 3, 38);
	}
	for (unsigned depth : {8U, 16U}) {
		for (unsigned color : {2U, 6U}) {
			check(depth, color, 3, 0);
			check(depth, color, 768, 0);
			check(depth, color, 4, 38);
		}
	}
	std::cout << "[PASS] PLTE length and indexed-depth limits agree in decode and inspection; valid pixels retained\n";
}

void checkDirectResult(Decoder decoder, const Bytes& compressed, size_t bound,
	std::optional<unsigned> expected_error, const Bytes& expected = {}) {
	LodePNGDecompressSettings settings;
	lodepng_decompress_settings_init(&settings);
	settings.max_output_size = bound;
	unsigned char* output = nullptr;
	size_t output_size = 0;
	const unsigned error = decoder(&output, &output_size, compressed.data(), compressed.size(), &settings);
	const std::unique_ptr<unsigned char, decltype(&std::free)> allocation(output, std::free);
	if (expected_error ? error != *expected_error : error == 0) {
		throw std::runtime_error(std::string(decoder == lodepng_zlib_adapter::decompress ? "Fast" : "Streaming") +
			" decompression with bound " + std::to_string(bound) +
			", input bytes " + std::to_string(compressed.size()) +
			(expected_error ? ": expected error " + std::to_string(*expected_error) : ": expected failure") +
			", got " + std::to_string(error));
	}
	if (error != 0) {
		requireTest(output == nullptr && output_size == 0, "Failed decompression exposed partial output.");
	} else {
		requireTest(output_size == expected.size() &&
			std::equal(expected.begin(), expected.end(), output), "Direct decompression returned wrong bytes.");
	}
	requireTest(settings.max_output_size == bound, "Direct decompression mutated its caller's bound.");
}

void checkDirectAdapters() {
	for (Decoder decoder : {lodepng_zlib_adapter::decompress,
		lodepng_zlib_adapter::decompressZlibStreaming}) {
		for (size_t bound : {size_t{1}, size_t{4096}}) {
			for (size_t count : {bound - 1, bound, bound + 1}) {
				const Bytes raw(count, 0x63);
				checkDirectResult(decoder, zlibStream(raw), bound,
					count > bound ? OUTPUT_LIMIT_ERROR : 0, raw);
			}
		}
		const Bytes raw(8192, 0x7b);
		const Bytes compressed = zlibStream(raw);
		checkDirectResult(decoder, compressed, 0, 0, raw);
		Bytes bad_checksum = compressed;
		bad_checksum.back() ^= 1U;
		checkDirectResult(decoder, bad_checksum, raw.size(), std::nullopt);
		Bytes truncated = compressed;
		truncated.pop_back();
		// libdeflate can report insufficient space for a truncated stream when
		// the exact-size buffer fills before it can identify the missing trailer.
		// Both backends must reject it without exposing output; the error number
		// need not agree for an invalid stream.
		checkDirectResult(decoder, truncated, raw.size(), std::nullopt);
		Bytes trailing = compressed;
		trailing.push_back(0);
		checkDirectResult(decoder, trailing, raw.size(), lodepng_zlib_adapter::ERROR_TRAILING_DATA);
		Bytes concatenated = compressed;
		concatenated.insert(concatenated.end(), compressed.begin(), compressed.end());
		checkDirectResult(decoder, concatenated, raw.size(), lodepng_zlib_adapter::ERROR_TRAILING_DATA);
	}
	std::cout << "[PASS] both adapters handle below/exact/above bounds, empty output, and unknown sizes\n";
	std::cout << "[PASS] both adapters reject bad checksums, truncation, trailing bytes, and concatenated streams\n";
}

} // namespace

extern "C" int __real_inflateInit_(z_streamp, const char*, int);
extern "C" int __wrap_inflateInit_(z_streamp stream, const char* version, int stream_size) {
	++streaming_calls;
	return __real_inflateInit_(stream, version, stream_size);
}

extern "C" libdeflate_decompressor* __real_libdeflate_alloc_decompressor();
extern "C" libdeflate_decompressor* __wrap_libdeflate_alloc_decompressor() {
	return force_streaming ? nullptr : __real_libdeflate_alloc_decompressor();
}

int main() {
	try {
		checkGeometryBounds();
		checkPngRejectionAndSettings();
		checkHighCompressionFastPath();
		checkPaletteValidation();
		checkDirectAdapters();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "[FAIL] " << error.what() << '\n';
		return 1;
	}
}
