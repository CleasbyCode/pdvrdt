#include "conceal.h"
#include "encryption.h"
#include "image.h"
#include "io_utils.h"
#include "key_codec.h"
#include "png_utils.h"
#include "reddit_steg.h"
#include "compression.h"
#include "step_output.h"

#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <format>
#include <limits>
#include <print>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

// Profile template for default mode: data stored in a custom IDAT chunk.
constexpr auto DEFAULT_PROFILE = std::to_array<Byte>({
	0x75, 0x5D, 0x19, 0x3D, 0x72, 0xCE, 0x28, 0xA5, 0x60, 0x59, 0x17, 0x98, 0x13, 0x40, 0xB4, 0xDB,
	0x3D, 0x18, 0xEC, 0x10, 0xFA, 0xE8, 0xA1, 0xC3, 0x99, 0xD1, 0xCC, 0x34, 0x72, 0xA3, 0xC5, 0xB1,
	0xEF, 0xF6, 0x12, 0x18, 0x26, 0xF3, 0xAF, 0x77, 0x16, 0x44, 0x95, 0xEA, 0xBB, 0x27, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0x50, 0x3C, 0xEA, 0x5E, 0x9D, 0xF9, 0x90, 0x82
});

// ICC color profile template for Mastodon mode: data stored in an iCCP chunk.
constexpr auto MASTODON_PROFILE = std::to_array<Byte>({
	0x00, 0x00, 0x02, 0x98, 0x6C, 0x63, 0x6D, 0x73, 0x02, 0x10, 0x00, 0x00, 0x6D, 0x6E, 0x74, 0x72,
	0x52, 0x47, 0x42, 0x20, 0x58, 0x59, 0x5A, 0x20, 0x07, 0xE2, 0x00, 0x03, 0x00, 0x14, 0x00, 0x09,
	0x00, 0x0E, 0x00, 0x1D, 0x61, 0x63, 0x73, 0x70, 0x4D, 0x53, 0x46, 0x54, 0x00, 0x00, 0x00, 0x00,
	0x73, 0x61, 0x77, 0x73, 0x63, 0x74, 0x72, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF6, 0xD6, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0xD3, 0x2D,
	0x68, 0x61, 0x6E, 0x64, 0xEB, 0x77, 0x1F, 0x3C, 0xAA, 0x53, 0x51, 0x02, 0xE9, 0x3E, 0x28, 0x6C,
	0x91, 0x46, 0xAE, 0x57, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x09, 0x64, 0x65, 0x73, 0x63, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x1C,
	0x77, 0x74, 0x70, 0x74, 0x00, 0x00, 0x01, 0x0C, 0x00, 0x00, 0x00, 0x14, 0x72, 0x58, 0x59, 0x5A,
	0x00, 0x00, 0x01, 0x20, 0x00, 0x00, 0x00, 0x14, 0x67, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x34,
	0x00, 0x00, 0x00, 0x14, 0x62, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x48, 0x00, 0x00, 0x00, 0x14,
	0x72, 0x54, 0x52, 0x43, 0x00, 0x00, 0x01, 0x5C, 0x00, 0x00, 0x00, 0x34, 0x67, 0x54, 0x52, 0x43,
	0x00, 0x00, 0x01, 0x5C, 0x00, 0x00, 0x00, 0x34, 0x62, 0x54, 0x52, 0x43, 0x00, 0x00, 0x01, 0x5C,
	0x00, 0x00, 0x00, 0x34, 0x63, 0x70, 0x72, 0x74, 0x00, 0x00, 0x01, 0x90, 0x00, 0x00, 0x00, 0x01,
	0x64, 0x65, 0x73, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x6E, 0x52, 0x47, 0x42,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0x59, 0x5A, 0x20,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x54, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x16, 0xC9,
	0x58, 0x59, 0x5A, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6F, 0xA0, 0x00, 0x00, 0x38, 0xF2,
	0x00, 0x00, 0x03, 0x8F, 0x58, 0x59, 0x5A, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x62, 0x96,
	0x00, 0x00, 0xB7, 0x89, 0x00, 0x00, 0x18, 0xDA, 0x58, 0x59, 0x5A, 0x20, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x24, 0xA0, 0x00, 0x00, 0x0F, 0x85, 0x00, 0x00, 0xB6, 0xC4, 0x63, 0x75, 0x72, 0x76,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x01, 0x07, 0x02, 0xB5, 0x05, 0x6B,
	0x09, 0x36, 0x0E, 0x50, 0x14, 0xB1, 0x1C, 0x80, 0x25, 0xC8, 0x30, 0xA1, 0x3D, 0x19, 0x4B, 0x40,
	0x5B, 0x27, 0x6C, 0xDB, 0x80, 0x6B, 0x95, 0xE3, 0xAD, 0x50, 0xC6, 0xC2, 0xE2, 0x31, 0xFF, 0xFF,
	0x00, 0x12, 0xB7, 0x19, 0x18, 0xA4, 0xEF, 0x15, 0x8F, 0x9E, 0x7B, 0xB4, 0xF3, 0xAA, 0x0A, 0x5C,
	0x80, 0x54, 0xAF, 0xC8, 0x0E, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0x50, 0x3C, 0xEA, 0x5E, 0x9D, 0xF9, 0x90
});

struct PlatformLimits {
	std::string_view name;
	std::size_t max_size;
	bool requires_good_dims;
};

constexpr std::array PLATFORM_LIMITS = {
	PlatformLimits{ "Flickr",    200uz * 1024 * 1024, false },
	PlatformLimits{ "ImgBB",      32uz * 1024 * 1024, false },
	PlatformLimits{ "PostImage",  32uz * 1024 * 1024, false },
	PlatformLimits{ "ImgPile",     8uz * 1024 * 1024, false },
	PlatformLimits{ "X-Twitter",   5uz * 1024 * 1024, true  },
};

constexpr std::size_t
	DEFAULT_IDAT_PREFIX_BYTES        = 3,
	MASTODON_ICCP_PREFIX_BYTES       = 5,
	BYTES_PER_KIBIBYTE               = 1024,
	DATA_FILENAME_MAX_LENGTH         = 20,
	// PNG spec 5.3: a chunk's length field "must not exceed 2^31-1", not the
	// 2^32-1 the four-byte field could hold. The output size limits happen to keep
	// every chunk we emit under this today, but by only ~80 bytes -- so enforce it
	// directly rather than leaving spec compliance resting on that margin.
	PNG_MAX_CHUNK_DATA_SIZE          = 0x7FFFFFFFuz;

[[nodiscard]] std::pair<std::size_t, std::string_view> sizeLimitForOption(Option option) {
	constexpr std::size_t
		MAX_SIZE_DEFAULT  = 2uz * 1024 * 1024 * 1024,
		MAX_SIZE_MASTODON = 16uz * 1024 * 1024;

	switch (option) {
		case Option::Mastodon: return { MAX_SIZE_MASTODON, "Mastodon" };
		case Option::Reddit:   return { REDDIT_UPLOAD_SIZE_LIMIT, "Reddit" };
		default:               return { MAX_SIZE_DEFAULT,  "pdvrdt" };
	}
}

void validateSizeLimit(std::size_t size, Option option, std::string_view subject) {
	const auto [limit, label] = sizeLimitForOption(option);
	if (size > limit) {
		throw std::runtime_error(std::format(
			"File Size Error: {} exceeds maximum size limit for {}.", subject, label));
	}
}

// What is left of the platform's output budget once the cover image and the
// fixed cost of the chunk carrying the payload are accounted for.
[[nodiscard]] std::size_t payloadBudget(
	std::size_t optimized_png_size,
	Option option,
	std::size_t chunk_prefix_bytes) {

	const auto [output_limit, label] = sizeLimitForOption(option);
	const std::size_t fixed_output_size = checkedAddSize(
		optimized_png_size,
		PNG_CHUNK_OVERHEAD + chunk_prefix_bytes,
		"File Size Error: Final output size overflow."
	);
	if (fixed_output_size >= output_limit) {
		throw std::runtime_error(std::format(
			"File Size Error: Cover image leaves no payload capacity for {}.", label));
	}
	return output_limit - fixed_output_size;
}

[[nodiscard]] std::size_t maximumMastodonCompressedProfileSize(std::size_t optimized_png_size) {
	return payloadBudget(optimized_png_size, Option::Mastodon, MASTODON_ICCP_PREFIX_BYTES);
}

[[nodiscard]] std::size_t maximumProfileSizeForEncryption(
	std::size_t optimized_png_size,
	Option option) {

	if (option == Option::Mastodon) {
		// The Mastodon profile is *stored* (level 0) into its iCCP chunk, so the
		// compressed form is never smaller than the profile itself -- the compressed
		// budget is therefore a valid upper bound on the profile too. Failing
		// against it here aborts encryption as soon as the payload provably cannot
		// fit, instead of building up to the 64 MiB recovery ceiling first and only
		// then discovering it is ~4x over the Mastodon limit.
		//
		// MAX_MASTODON_PROFILE_BYTES stays as the other half of the min: conceal
		// must never emit an image whose profile its own recovery path would refuse
		// to inflate.
		return std::min(
			MAX_MASTODON_PROFILE_BYTES,
			maximumMastodonCompressedProfileSize(optimized_png_size));
	}
	return payloadBudget(optimized_png_size, option, DEFAULT_IDAT_PREFIX_BYTES);
}

void validateDataFilename(const fs::path& data_file_path, const std::string& data_filename) {
	if (const std::string_view problem = embeddedFilenameProblem(data_file_path.filename());
		!problem.empty()) {
		throw std::runtime_error(std::format(
			"Data File Error: Embedded filename is unsafe: {}.", problem));
	}
	if (data_filename.size() > DATA_FILENAME_MAX_LENGTH) {
		throw std::runtime_error("Data File Error: For compatibility requirements, data filename must not exceed 20 bytes (UTF-8). "
			"Non-ASCII characters use 2-4 bytes each.");
	}
}

// Names the format when the data file's leading bytes show one that is already
// entropy-coded, so a deflate pass gains ~0%; empty otherwise. Such a payload is
// stored in a level-0 zlib stream instead of being deflated. The header, not the
// extension, decides: a renamed archive is still recognised, and a text file
// named ".zip" is still compressed. Executables are deliberately absent: they
// typically deflate by 30-50%.
constexpr std::size_t FORMAT_HEADER_SIZE = 16;

[[nodiscard]] std::string_view detectCompressedFormat(std::span<const Byte> header) {
	const auto has = [header](std::string_view signature, std::size_t at = 0) {
		return header.size() >= at + signature.size()
			&& std::equal(signature.begin(), signature.end(), header.begin() + static_cast<std::ptrdiff_t>(at),
				[](char a, Byte b) { return static_cast<Byte>(a) == b; });
	};
	using namespace std::string_view_literals;

	// Archives and compressed streams (ZIP also covers jar, apk, docx, odt, epub).
	if (has("PK\x03\x04"sv) || has("PK\x05\x06"sv) || has("PK\x07\x08"sv)) return "ZIP";
	if (has("Rar!\x1a\x07"sv)) return "RAR";
	if (has("7z\xbc\xaf\x27\x1c"sv)) return "7-Zip";
	if (has("\x1f\x8b"sv)) return "gzip";
	if (has("BZh"sv) && header.size() > 3 && header[3] >= '1' && header[3] <= '9') return "bzip2";
	if (has("\xfd" "7zXZ\x00"sv)) return "xz";
	if (has("LZIP"sv)) return "lzip";
	if (has("\x04\x22\x4d\x18"sv)) return "LZ4";
	if (has("\x28\xb5\x2f\xfd"sv)) return "Zstandard";
	if (has("MSCF\x00\x00\x00\x00"sv)) return "CAB";
	if (has("\xed\xab\xee\xdb"sv)) return "RPM";
	if (has("!<arch>\ndebian"sv)) return "Debian package";
	// Images.
	if (has("\xff\xd8\xff"sv)) return "JPEG";
	if (has("\x89PNG\r\n\x1a\n"sv)) return "PNG";
	if (has("GIF87a"sv) || has("GIF89a"sv)) return "GIF";
	if (has("RIFF"sv) && has("WEBP"sv, 8)) return "WebP";
	if (has("\xff\x0a"sv) || has("\x00\x00\x00\x0cJXL \r\n\x87\n"sv)) return "JPEG XL";
	// Audio and video. "ftyp" at offset 4 is ISO media: MP4, MOV, M4A, HEIC, AVIF.
	if (has("ftyp"sv, 4)) return "MP4/ISO media";
	if (has("\x1a\x45\xdf\xa3"sv)) return "Matroska/WebM";
	if (has("OggS"sv)) return "Ogg";
	if (has("fLaC"sv)) return "FLAC";
	if (has("ID3"sv)) return "MP3";
	// MPEG audio frame sync (MP3, ADTS AAC).
	if (header.size() >= 2 && header[0] == 0xFF && (header[1] & 0xE0) == 0xE0) return "MPEG audio";
	return {};
}

// Reads the data file's first bytes with pread, so the file offset the
// compressor starts from is untouched. A file shorter than the header is
// matched on what it has.
[[nodiscard]] std::string_view compressedFormatOf(const OpenInputFile& data_file) {
	std::array<Byte, FORMAT_HEADER_SIZE> header{};
	ScopedWipe header_wipe{header};
	const std::size_t wanted = std::min(header.size(), data_file.size());
	const PreadResult read = preadFill(data_file.fd(), std::span<Byte>(header).first(wanted), 0);
	if (read.error != 0 || read.filled < wanted) {
		throw std::runtime_error("Read Error: Unable to read the data file's header.");
	}
	return detectCompressedFormat(std::span<const Byte>(header).first(wanted));
}

[[nodiscard]] std::uint32_t checkedChunkDataSize(std::size_t payload_size, std::size_t chunk_diff) {
	if (chunk_diff > PNG_MAX_CHUNK_DATA_SIZE || payload_size > PNG_MAX_CHUNK_DATA_SIZE - chunk_diff) {
		throw std::runtime_error("PNG Error: Chunk payload exceeds PNG chunk size limit.");
	}
	return static_cast<std::uint32_t>(payload_size + chunk_diff);
}

[[nodiscard]] std::size_t checkedChunkTotalSize(std::size_t payload_size, std::size_t chunk_diff) {
	return static_cast<std::size_t>(checkedChunkDataSize(payload_size, chunk_diff)) + PNG_CHUNK_OVERHEAD;
}

// ---------------------------------------------------------------------------
// Conceal progress report (step helpers in step_output.h).
// ---------------------------------------------------------------------------

constexpr std::string_view ENCRYPTION_RESULT =
	"XChaCha20-Poly1305, 256-bit key derived by Argon2id from the 128-bit recovery KEY.";
constexpr std::string_view REDDIT_EMBED_RESULT =
	"Adaptive (1,15,4) matrix embedding --> Least significant bits of keyed RGB samples.";
// Data files above this size get the wait note on the compress step.
constexpr std::size_t LARGE_FILE_SIZE = 300uz * 1024 * 1024;

void printConcealHeader(
	const fs::path& image_file_path,
	std::size_t cover_size,
	std::string_view data_filename,
	std::size_t data_size) {

	std::println("\nCover image: {} ({}).", printableFilename(image_file_path.filename().string()), formatSize(cover_size));
	std::println("Data file:   {} ({}).", printableFilename(data_filename), formatSize(data_size));
	std::print("\n");
}

// The pixel format an IHDR (and, for an indexed image, its PLTE) declares.
struct PngFormat {
	std::uint32_t width{};
	std::uint32_t height{};
	Byte bit_depth{};
	Byte color_type{};
	bool interlaced{};
	std::size_t palette_entries{};
};

[[nodiscard]] PngFormat readPngFormat(std::span<const Byte> png) {
	constexpr Byte INDEXED = 3;
	constexpr std::uint32_t TYPE_PLTE = 0x504C5445u;
	const PngChunkView ihdr = readRequiredIhdr(png);
	PngFormat format{
		.width = getValue(ihdr.data, 0),
		.height = getValue(ihdr.data, 4),
		.bit_depth = ihdr.data[8],
		.color_type = ihdr.data[9],
		.interlaced = ihdr.data[12] != 0,
	};
	if (format.color_type == INDEXED) {
		forEachChunkToIend(png, ihdr.offset + ihdr.total_size, [&format](const PngChunkView& chunk) {
			if (chunk.type == TYPE_PLTE) format.palette_entries = chunk.length / 3;
		});
	}
	return format;
}

// "8-bit RGBA", "4-bit indexed (12 colors)", "16-bit RGB, Adam7 interlaced".
[[nodiscard]] std::string pngFormatText(const PngFormat& format) {
	std::string_view type = "unknown color type";
	switch (format.color_type) {
		case 0: type = "grayscale"; break;
		case 2: type = "RGB"; break;
		case 3: type = "indexed"; break;
		case 4: type = "grayscale+alpha"; break;
		case 6: type = "RGBA"; break;
		default: break;
	}
	std::string text = std::format("{}-bit {}", format.bit_depth, type);
	if (format.color_type == 3) {
		text += std::format(" ({} color{})", format.palette_entries, format.palette_entries == 1 ? "" : "s");
	}
	if (format.interlaced) text += ", Adam7 interlaced";
	return text;
}

// "1920x1080, 8-bit RGBA -> 8-bit indexed (200 colors), metadata removed."
[[nodiscard]] std::string describeCover(
	const PngFormat& source,
	const PngFormat& output,
	std::string_view metadata) {

	const std::string from = pngFormatText(source);
	const std::string to = pngFormatText(output);
	return std::format("{}x{}, {}, {}.",
		output.width, output.height,
		from == to ? from : std::format("{} -> {}", from, to),
		metadata);
}

// Result text for the "Compressing data file" step. The payload is always a
// zlib stream: stored (level 0) when the header shows compressed data.
[[nodiscard]] std::string describeCompression(
	std::size_t source_data_size,
	std::size_t stored_size,
	std::string_view compressed_format) {

	if (!compressed_format.empty()) {
		return std::format("skipped (file header shows {} data, already compressed).", compressed_format);
	}
	if (stored_size >= source_data_size) {
		return std::format("{} -> {} (no reduction).", formatSize(source_data_size), formatSize(stored_size));
	}
	return std::format("{} -> {} ({} smaller).",
		formatSize(source_data_size),
		formatSize(stored_size),
		percentText(source_data_size - stored_size, source_data_size));
}

// A closed stdout pipe would normally terminate the process with SIGPIPE before
// writeOutputFile() can remove its newly-created PNG. SIGPIPE is ignored for
// that whole transaction -- every step line, the platform list and the KEY
// report, from the output file's creation to the end of the report -- so a
// stream failure becomes an ordinary error and takes the same cleanup path as
// a file-write failure.
struct SigpipeIgnoreGuard {
	struct sigaction old_action{};
	bool active{false};

	SigpipeIgnoreGuard(const SigpipeIgnoreGuard&) = delete;
	SigpipeIgnoreGuard& operator=(const SigpipeIgnoreGuard&) = delete;

	SigpipeIgnoreGuard() {
		struct sigaction ignore_action{};
		ignore_action.sa_handler = SIG_IGN;
		if (sigemptyset(&ignore_action.sa_mask) != 0 ||
			::sigaction(SIGPIPE, &ignore_action, &old_action) != 0) {
			const std::error_code ec(errno, std::generic_category());
			throw std::runtime_error(std::format(
				"Output Error: Unable to protect output reporting: {}", ec.message()));
		}
		active = true;
	}

	~SigpipeIgnoreGuard() {
		if (active) {
			::sigaction(SIGPIPE, &old_action, nullptr);
		}
	}

	void finish() {
		if (!active) return;
		if (::sigaction(SIGPIPE, &old_action, nullptr) != 0) {
			const std::error_code ec(errno, std::generic_category());
			throw std::runtime_error(std::format(
				"Output Error: Unable to restore SIGPIPE handling: {}", ec.message()));
		}
		active = false;
	}
};

void flushStdoutOrThrow() {
	errno = 0;
	const int flush_result = std::fflush(stdout);
	const int saved_errno = errno;
	if (flush_result != 0 || std::ferror(stdout) != 0) {
		const std::error_code ec(saved_errno != 0 ? saved_errno : EIO, std::generic_category());
		throw std::runtime_error(std::format(
			"Output Error: Unable to reliably report the output filename and recovery KEY: {}",
			ec.message()));
	}
}

void reportOutputOrThrow(
	const fs::path& output_path,
	std::size_t output_size,
	const RecoveryKey& recovery_key) {
	// Runs under writeOutputFile()'s SigpipeIgnoreGuard.
	// Verify all earlier informational output reached the stream before adding
	// the success report.  stdio error indicators are sticky, so this also
	// catches failures from the compatibility report above.
	flushStdoutOrThrow();

	KeyDisplay key_text{};
	ScopedWipe key_text_wiper{key_text};
	encodeKeyDisplay(recovery_key, key_text);
	std::string report = std::format(
		"\nSaved \"file-embedded\" PNG image: {} ({} bytes).\n"
		"\nRecovery KEY: [***{}***]\n\nImportant: Keep your KEY safe, so that you can extract the hidden file.\n\nComplete!\n\n",
		output_path.string(), output_size, std::string_view(key_text.data(), key_text.size()));
	ScopedWipe report_wiper{report};

	// Written straight to the descriptor, not through stdio: fwrite() would copy
	// the KEY into stdout's buffer, which lives until exit and is never wiped.
	try {
		writeAllToFd(STDOUT_FILENO, std::span<const Byte>(
			reinterpret_cast<const Byte*>(report.data()), report.size()));
	} catch (const std::exception& e) {
		throw std::runtime_error(std::format(
			"Output Error: Unable to reliably report the output filename and recovery KEY: {}", e.what()));
	}
}

// Streams one PNG chunk to `fd`: length and type first, the data in any number
// of pieces, then the CRC. The data never has to be assembled in memory first.
class ChunkWriter {
public:
	ChunkWriter(int fd, std::uint32_t type, std::uint32_t data_size) : fd_(fd), remaining_(data_size) {
		std::array<Byte, 8> header{};
		updateValue(header, 0, data_size);
		updateValue(header, 4, type);
		writeAllToFd(fd_, header);
		crc_ = pdvrdtCrc32Update(0, std::span<const Byte>(header).subspan(4));
	}

	void write(std::span<const Byte> data) {
		if (data.size() > remaining_) {
			throw std::logic_error("PNG Error: Chunk data exceeds its declared length.");
		}
		writeAllToFd(fd_, data);
		crc_ = pdvrdtCrc32Update(crc_, data);
		remaining_ -= data.size();
	}

	void finish() {
		if (remaining_ != 0) {
			throw std::logic_error("PNG Error: Chunk data is shorter than its declared length.");
		}
		std::array<Byte, 4> crc_bytes{};
		updateValue(crc_bytes, 0, crc_);
		writeAllToFd(fd_, crc_bytes);
	}

private:
	int fd_;
	std::size_t remaining_;
	std::uint32_t crc_{};
};

// Streams one PNG chunk whose length is not known until its data has all been
// written: a placeholder header first, the data in any number of pieces
// (buffered, so a stream of small pieces costs few writes), then the CRC, and
// last the real length written back over the placeholder. The CRC covers the
// type and data, not the length, so it runs as the data goes out.
class StreamingChunkWriter {
public:
	StreamingChunkWriter(int fd, std::size_t header_offset, std::uint32_t type)
		: fd_(fd), header_offset_(header_offset) {
		std::array<Byte, 8> header{};
		updateValue(header, 4, type);
		writeAllToFd(fd_, header);
		crc_ = pdvrdtCrc32Update(0, std::span<const Byte>(header).subspan(4));
		buffer_.reserve(BUFFER_BYTES);
	}

	void write(std::span<const Byte> data) {
		if (data.size() > PNG_MAX_CHUNK_DATA_SIZE - data_size_) {
			throw std::runtime_error(
				"File Size Error: Compressed and encrypted payload exceeds the PNG chunk size limit.");
		}
		crc_ = pdvrdtCrc32Update(crc_, data);
		data_size_ += data.size();
		if (buffer_.size() + data.size() > BUFFER_BYTES) {
			flush();
		}
		if (data.size() >= BUFFER_BYTES) {
			writeAllToFd(fd_, data);
		} else {
			buffer_.insert(buffer_.end(), data.begin(), data.end());
		}
	}

	// Ends the chunk; returns its data length.
	[[nodiscard]] std::uint32_t finish() {
		flush();
		std::array<Byte, 4> bytes{};
		updateValue(bytes, 0, crc_);
		writeAllToFd(fd_, bytes);
		updateValue(bytes, 0, static_cast<std::uint32_t>(data_size_));
		std::size_t done = 0;
		while (done < bytes.size()) {
			const ssize_t wrote = ::pwrite(fd_, bytes.data() + done, bytes.size() - done,
				static_cast<off_t>(header_offset_ + done));
			if (wrote < 0 && errno == EINTR) continue;
			if (wrote <= 0) {
				const std::error_code ec(wrote < 0 ? errno : EIO, std::generic_category());
				throw std::runtime_error(std::format("Write Error: Failed to write output file: {}", ec.message()));
			}
			done += static_cast<std::size_t>(wrote);
		}
		return static_cast<std::uint32_t>(data_size_);
	}

private:
	static constexpr std::size_t BUFFER_BYTES = 4uz * 1024 * 1024;

	void flush() {
		writeAllToFd(fd_, buffer_);
		buffer_.clear();
	}

	int fd_;
	std::size_t header_offset_;
	std::size_t data_size_{};
	std::uint32_t crc_{};
	vBytes buffer_;
};

void verifyFdSize(int fd, std::size_t expected_size) {
	struct stat st{};
	if (::fstat(fd, &st) != 0) {
		const std::error_code ec(errno, std::generic_category());
		throw std::runtime_error(std::format("Write Error: Unable to verify output file size: {}", ec.message()));
	}
	if (st.st_size < 0 || static_cast<std::uintmax_t>(st.st_size) != static_cast<std::uintmax_t>(expected_size)) {
		throw std::runtime_error(std::format(
			"Write Error: Output file size mismatch. Expected {} bytes, wrote {} bytes.",
			expected_size,
			st.st_size < 0 ? 0 : static_cast<std::uintmax_t>(st.st_size)));
	}
}

std::vector<std::string> getCompatiblePlatforms(Option option, std::size_t output_size, bool has_bad_dims, bool twitter_iccp_compatible) {

	if (option == Option::Mastodon) {
		if (twitter_iccp_compatible && !has_bad_dims) {
			return { "Mastodon and X-Twitter." };
		}
		return { "Mastodon. (Only share this \"file-embedded\" PNG image on Mastodon)." };
	}
	std::vector<std::string> platforms;
	for (const auto& [name, max_size, needs_good_dims] : PLATFORM_LIMITS) {
		if (output_size <= max_size && (!needs_good_dims || !has_bad_dims)) {
			platforms.emplace_back(name);
		}
	}

	if (platforms.empty()) {
		platforms.emplace_back(
			"Unknown!\n\n Due to the large file size of the output PNG image, I'm unaware of any\n"
			" compatible platforms that this image can be posted on. Local use only?");
	}

	return platforms;
}

// A Reddit image also posts to Mastodon when it is within Mastodon's upload
// size limit and 8.3 megapixels (3840x2160 worth of pixels).
constexpr std::uint64_t MASTODON_MAX_PIXELS = 3840ull * 2160;

std::vector<std::string> getRedditCompatiblePlatforms(
	std::size_t output_size,
	std::uint32_t width,
	std::uint32_t height) {

	if (output_size <= sizeLimitForOption(Option::Mastodon).first &&
		static_cast<std::uint64_t>(width) * height <= MASTODON_MAX_PIXELS) {
		return { "Reddit and Mastodon." };
	}
	return { "Reddit. (Only share this \"file-embedded\" PNG image on Reddit)." };
}

void printPlatformCompatibility(const std::vector<std::string>& platforms) {
	std::println("\nPlatform compatibility for output image:-\n");
	for (const auto& platform : platforms) {
		std::println(" ✓ {}", platform);
	}
}

// What the last steps report. An empty embed_result means the payload was
// embedded before writing (Reddit), so the write is only the "Writing" step.
struct OutputReport {
	std::string embed_result;
	std::string write_result;
	std::vector<std::string> platforms;
};

// An output image while it is written: unnamed until published, so an
// interrupted write never leaves an image whose KEY was not shown (see
// OutputFile). SIGPIPE is ignored from its creation until the KEY has been
// reported, so a closed stdout surfaces as a write error that removes the
// image, never as a SIGPIPE that kills the process and leaves an image whose
// KEY nobody saw.
struct StagedOutput {
	SigpipeIgnoreGuard sigpipe_guard;
	OutputFile file = OutputFile::createInCurrentDirectory();
};

// Finish the image staged in `staged` -- `output_size` bytes, written in full --
// once the caller has begun the "Writing output image" step: verify, make it
// durable, give it its name, then report it and its KEY. Any failure from the
// name on removes it again.
void publishOutput(
	StagedOutput& staged,
	std::size_t output_size,
	const RecoveryKey& recovery_key,
	std::string_view write_result,
	const std::vector<std::string>& platforms) {
	constexpr std::size_t MAX_NAME_ATTEMPTS = 2048;

	OutputFile& output_file = staged.file;
	verifyFdSize(output_file.fd(), output_size);
	// Durability before the success report: the KEY is printed exactly once and
	// exists nowhere else, so an image still sitting in the page cache when the
	// machine loses power would be unrecoverable even though the user was told
	// the operation completed.
	fsyncFdOrThrow(output_file.fd());

	fs::path output_path;
	for (std::size_t i = 0; i < MAX_NAME_ATTEMPTS && output_path.empty(); ++i) {
		fs::path candidate = std::format("prdt_{}.png", 100000 + randombytes_uniform(900000));
		if (output_file.publish(candidate)) {
			output_path = std::move(candidate);
		}
	}
	if (output_path.empty()) {
		throw std::runtime_error("Write Error: Unable to allocate output filename.");
	}

	try {
		output_file.close();
		fsyncParentDirectoryNoThrow(output_path);
		endStep(write_result);
		printPlatformCompatibility(platforms);
		reportOutputOrThrow(output_path, output_size, recovery_key);
	} catch (const std::system_error& e) {
		// A failed std::print to stdout (e.g. a closed pipe, with SIGPIPE
		// ignored); same deliberate removal as below.
		cleanupPathNoThrow(output_path);
		throw std::runtime_error(std::format(
			"Output Error: Unable to reliably report the output filename and recovery KEY: {}",
			e.code().message()));
	} catch (...) {
		// Deliberate, including when reportOutputOrThrow() fails *after* a
		// complete image was written and closed: the KEY exists only in this
		// process and is wiped on the way out, so an image whose KEY never
		// reached the user can never be recovered by anyone. Leaving it behind
		// would only be a confusing, permanently opaque file. Do not "fix" this
		// into keeping the output.
		cleanupPathNoThrow(output_path);
		throw;
	}
	// The KEY has been delivered, so the image stays whatever happens here.
	staged.sigpipe_guard.finish();
}

// Write a whole image with `writer`, for an image already built (Reddit) or
// built while it is written from an in-memory profile (Mastodon).
template <typename Writer>
void writeOutputFile(
	std::size_t output_size,
	const RecoveryKey& recovery_key,
	const OutputReport& report,
	Writer&& writer) {

	StagedOutput staged;
	const bool embeds_while_writing = !report.embed_result.empty();
	beginStep(embeds_while_writing ? "Embedding" : "Writing output image");
	std::forward<Writer>(writer)(staged.file.fd());
	if (embeds_while_writing) {
		endStep(report.embed_result);
		beginStep("Writing output image");
	}
	publishOutput(staged, output_size, recovery_key, report.write_result, report.platforms);
}

[[nodiscard]] vBytes makeProfileTemplate(bool is_mastodon) {
	if (is_mastodon) {
		return vBytes(MASTODON_PROFILE.begin(), MASTODON_PROFILE.end());
	}
	return vBytes(DEFAULT_PROFILE.begin(), DEFAULT_PROFILE.end());
}

void writePngTailFrom(int fd, const vBytes& png_vec, std::size_t offset) {
	writeAllToFd(
		fd,
		std::span<const Byte>(
			png_vec.data() + static_cast<std::ptrdiff_t>(offset),
			png_vec.size() - offset
		)
	);
}

void writeMastodonOutput(
	const vBytes& png_vec,
	vBytes& profile_vec,
	Option option,
	bool has_bad_dims,
	const RecoveryKey& recovery_key) {

	constexpr std::size_t
		MASTODON_INSERT_INDEX     = 0x21,
		TWITTER_ICCP_MAX_CHUNK_SIZE = 10uz * 1024,
		TWITTER_IMAGE_MAX_SIZE      = 5uz * 1024 * 1024,
		ICCP_SIZE_DIFF              = MASTODON_ICCP_PREFIX_BYTES,
		ICC_PROFILE_SIZE_FIELD_BYTES = 4;

	if (MASTODON_INSERT_INDEX > png_vec.size()) {
		throw std::runtime_error("Image File Error: Invalid PNG insertion point for iCCP chunk.");
	}
	// The ICC header's first field is the profile's total size. Decoders such as
	// libpng read exactly that many bytes and reject or truncate the profile on
	// a mismatch, dropping the payload with it, so it must match the encrypted
	// profile's real length. Nothing authenticates or checks this field on
	// recovery, so images written before it was set still recover.
	if (profile_vec.size() < ICC_PROFILE_SIZE_FIELD_BYTES ||
		profile_vec.size() > std::numeric_limits<std::uint32_t>::max()) {
		throw std::runtime_error("Internal Error: Invalid Mastodon ICC profile size.");
	}
	updateValue(profile_vec, 0, static_cast<std::uint32_t>(profile_vec.size()));

	// iCCP requires its profile to be a zlib stream, so the encrypted profile has
	// to be wrapped -- but it must not be *deflated*. The profile is ciphertext,
	// which is incompressible by construction: level 6 measures ~190 ms per
	// 12 MiB and produces slightly more output than storing it (~2 ms). The
	// stored stream's size is known exactly up front, so it is streamed straight
	// into the chunk rather than built as a second in-memory copy.
	const std::size_t stored_profile_size = zlibStoredStreamSize(profile_vec.size());
	if (stored_profile_size > maximumMastodonCompressedProfileSize(png_vec.size())) {
		throw std::runtime_error(
			"File Size Error: Compressed profile exceeds the Mastodon output size limit.");
	}
	const std::uint32_t mastodon_chunk_data_size = checkedChunkDataSize(stored_profile_size, ICCP_SIZE_DIFF);

	const std::size_t output_size = checkedAddSize(
		png_vec.size(),
		checkedChunkTotalSize(stored_profile_size, ICCP_SIZE_DIFF),
		"File Size Error: Final output size overflow."
	);
	const bool twitter_iccp_compatible =
		output_size <= TWITTER_IMAGE_MAX_SIZE &&
		mastodon_chunk_data_size <= TWITTER_ICCP_MAX_CHUNK_SIZE;

	validateSizeLimit(output_size, option, "Final output PNG");
	const OutputReport report{
		.embed_result = std::format("iCCP chunk ({}).", formatSize(mastodon_chunk_data_size)),
		.write_result = std::format("{} ({} of Mastodon's {} MiB upload size limit).",
			formatSizePrecise(output_size),
			percentText(output_size, sizeLimitForOption(Option::Mastodon).first),
			sizeLimitForOption(Option::Mastodon).first / (1024 * 1024)),
		.platforms = getCompatiblePlatforms(option, output_size, has_bad_dims, twitter_iccp_compatible),
	};

	writeOutputFile(output_size, recovery_key, report, [&](int fd) {
		writeAllToFd(fd, std::span<const Byte>(png_vec.data(), MASTODON_INSERT_INDEX));
		ChunkWriter chunk(fd, TYPE_ICCP, mastodon_chunk_data_size);
		chunk.write(PDVRDT_ICCP_PREFIX);
		zlibStoreSpan(profile_vec, [&chunk](std::span<const Byte> piece) { chunk.write(piece); });
		chunk.finish();
		writePngTailFrom(fd, png_vec, MASTODON_INSERT_INDEX);
	});
}

// Default mode writes the image while the payload is compressed and
// encrypted: the cover up to its IEND, then one IDAT chunk whose data -- the
// IDAT prefix and the encrypted profile -- streams straight into the unnamed
// output file, then IEND. Memory stays at a few MiB of buffers however large
// the payload (up to the 2 GiB output limit), where building the profile first
// held all of its ciphertext. Nothing is published until every check passes.
void concealDefaultStreamed(
	const vBytes& png_vec,
	OpenInputFile& data_file,
	const std::string& data_filename,
	std::string_view compressed_format,
	Option option,
	bool has_bad_dims) {

	constexpr std::size_t IDAT_SIZE_DIFF = DEFAULT_IDAT_PREFIX_BYTES;
	if (png_vec.size() < PNG_CHUNK_OVERHEAD) {
		throw std::runtime_error("Image File Error: Invalid PNG insertion point for IDAT chunk.");
	}
	const std::size_t insert_index = png_vec.size() - PNG_CHUNK_OVERHEAD;  // the zero-length IEND
	const std::size_t max_profile_size = maximumProfileSizeForEncryption(png_vec.size(), option);
	vBytes profile_template = makeProfileTemplate(false);
	// Owns the KEY from generation until the write finishes or any later step
	// throws; encryptCompressedFileToSink() fills it in place.
	RecoveryKey recovery_key;

	// Compression, encryption and embedding are one streamed pass here,
	// reported as their three steps.
	beginStep("Compressing data file");
	const bool wait_shown = showWaitNote(data_file.size() > LARGE_FILE_SIZE);
	StagedOutput staged;
	const int fd = staged.file.fd();
	writeAllToFd(fd, std::span<const Byte>(png_vec.data(), insert_index));
	StreamingChunkWriter idat(fd, insert_index, TYPE_IDAT);
	idat.write(PDVRDT_IDAT_PREFIX);
	const std::size_t stored_size = encryptCompressedFileToSink(
		recovery_key,
		profile_template,
		data_file.fd(),
		data_file.size(),
		data_filename,
		!compressed_format.empty(),
		max_profile_size,
		[&idat](std::span<const Byte> piece) { idat.write(piece); });
	endStepAfterWait(wait_shown, describeCompression(data_file.size(), stored_size, compressed_format));
	beginStep("Encrypting");
	endStep(ENCRYPTION_RESULT);

	beginStep("Embedding");
	const std::uint32_t idat_chunk_data_size = idat.finish();
	writePngTailFrom(fd, png_vec, insert_index);
	const std::size_t output_size = checkedAddSize(
		png_vec.size(),
		checkedChunkTotalSize(idat_chunk_data_size - IDAT_SIZE_DIFF, IDAT_SIZE_DIFF),
		"File Size Error: Final output size overflow.");
	validateSizeLimit(output_size, option, "Final output PNG");
	endStep(std::format("Final IDAT chunk ({}).", formatSize(idat_chunk_data_size)));

	beginStep("Writing output image");
	publishOutput(
		staged,
		output_size,
		recovery_key,
		std::format("{}.", formatSizePrecise(output_size)),
		getCompatiblePlatforms(option, output_size, has_bad_dims, false));
}

// The cover's own 16 MiB limit was enforced when it was read (see
// FileTypeCheck::reddit_cover_image); only the payload input is left to check.
void validateRedditPayloadInputSize(std::size_t payload_size) {
	if (payload_size > REDDIT_UPLOAD_SIZE_LIMIT) {
		throw std::runtime_error(std::format(
			"Data File Size Error: Payload file exceeds Reddit's {} MiB upload size limit.",
			REDDIT_UPLOAD_SIZE_LIMIT / (1024 * 1024)));
	}
}

[[nodiscard]] SecureBytes compressRedditPayload(
	int data_fd,
	std::size_t data_file_size,
	bool is_compressed_file) {

	// `compressed` holds the secret -- for a stored payload, byte for byte -- and
	// SecureBytes scrubs it on every exit, including a read that fails after
	// some output has already been appended. Sized up front (exact for stored
	// output, an upper bound for deflated) so it never needs to grow; if it
	// ever does, append() scrubs the buffer it leaves behind.
	SecureBytes compressed;
	compressed.reserve(zlibPayloadStreamBound(data_file_size, is_compressed_file));
	zlibDeflateFd(
		data_fd,
		data_file_size,
		is_compressed_file,
		[&](std::span<const Byte> chunk) {
			compressed.append(
				chunk,
				"Data File Size Error: Reddit compressed payload size overflow.");
		});
	if (compressed.empty()) {
		throw std::runtime_error(
			"File Size Error: File is zero bytes. Probable compression failure.");
	}
	return compressed;
}

[[nodiscard]] RedditPngCarrier prepareRedditCarrier(vBytes& png_vec) {
	// Validation only -- not optimizeImage(). The Reddit carrier decodes the
	// cover itself to opaque RGB8 and its fresh encoder intentionally retains no
	// metadata chunks, so an optimized image would be built and then dropped one
	// line below, along with a second full decode of the same pixels.
	prepareCoverForRedditCarrier(png_vec);
	RedditPngCarrier carrier = prepareRedditPngCarrier(png_vec);
	vBytes{}.swap(png_vec);
	return carrier;
}

struct RedditCapacityLimits {
	std::size_t conservative_compressed{};
	std::size_t recommended_compressed{};
};

[[nodiscard]] std::size_t maxRedditCompressedPayload(
	std::size_t carrier_capacity,
	std::size_t filename_length) {

	const auto fits = [&](std::size_t compressed_size) {
		return computeRedditEncryptedProfileSize(
			compressed_size,
			filename_length) <= carrier_capacity;
	};
	if (!fits(0)) return 0;

	std::size_t lower = 0;
	std::size_t upper = carrier_capacity;
	while (lower < upper) {
		const std::size_t middle = lower + (upper - lower + 1) / 2;
		if (fits(middle)) {
			lower = middle;
		} else {
			upper = middle - 1;
		}
	}
	return lower;
}

[[nodiscard]] RedditCapacityLimits redditCapacityLimits(
	const RedditPngCarrier& carrier) {

	const std::size_t conservative_compressed = maxRedditCompressedPayload(
		carrier.payload_capacity,
		DATA_FILENAME_MAX_LENGTH);
	return RedditCapacityLimits{
		.conservative_compressed = conservative_compressed,
		.recommended_compressed = conservative_compressed > BYTES_PER_KIBIBYTE
			? conservative_compressed - BYTES_PER_KIBIBYTE
			: 0,
	};
}

// "843 bytes", or "1401 bytes (1.4KiB)" once there is a larger unit to show.
[[nodiscard]] std::string bytesWithSize(std::size_t bytes) {
	return bytes < BYTES_PER_KIBIBYTE
		? std::format("{} byte{}", bytes, bytes == 1 ? "" : "s")
		: std::format("{} bytes ({})", bytes, formatSizePrecise(bytes));
}

[[nodiscard]] std::string describeRedditPayloadLimits(const RedditCapacityLimits& limits) {
	if (limits.conservative_compressed == 0) {
		return "no payload fits this cover image.";
	}
	if (limits.recommended_compressed == 0) {
		return std::format(
			"{} conservative maximum for a 20-byte filename, too small to keep the 1KiB of headroom.",
			bytesWithSize(limits.conservative_compressed));
	}
	return std::format(
		"{} conservative maximum, {} recommended, for a 20-byte filename.",
		bytesWithSize(limits.conservative_compressed),
		bytesWithSize(limits.recommended_compressed));
}

// The payload does not fit this cover with this filename: a hard limit, so
// report the real figures for the supplied filename rather than the generic
// 20-byte-filename guidance that capsize prints.
[[noreturn]] void reportRedditCapacityFailure(
	const RedditPngCarrier& carrier,
	std::size_t source_data_size,
	std::string_view compressed_format,
	std::size_t stored_payload_size,
	std::size_t filename_size,
	std::size_t encrypted_profile_size) {

	constexpr int LABEL_WIDTH = 57;
	const std::size_t overhead_size = encrypted_profile_size - stored_payload_size;
	const std::size_t max_stored_payload = maxRedditCompressedPayload(
		carrier.payload_capacity,
		filename_size);
	const std::size_t bytes_over = stored_payload_size > max_stored_payload
		? stored_payload_size - max_stored_payload
		: 1;
	const std::size_t recommended_payload = max_stored_payload > BYTES_PER_KIBIBYTE
		? max_stored_payload - BYTES_PER_KIBIBYTE
		: 0;
	// The payload is always a zlib stream; say which kind, and whether
	// deflating actually reduced it.
	const std::string storage_note = !compressed_format.empty()
		? std::format("level-0 zlib stream of the {}-byte file; its header shows {} data, already compressed",
			source_data_size, compressed_format)
		: stored_payload_size < source_data_size
			? std::format("compressed by pdvrdt from {} bytes", source_data_size)
			: std::format("zlib stream of the {}-byte file; compression did not reduce it", source_data_size);

	std::print("\n");
	std::println("{:<{}}{} bytes ({}).", "Stored payload size:", LABEL_WIDTH,
		stored_payload_size, storage_note);
	std::println("{:<{}}{} bytes ({}-byte filename).", "Encryption and carrier overhead:", LABEL_WIDTH,
		overhead_size, filename_size);
	std::println("{:<{}}{} bytes.", "Total encrypted profile size:", LABEL_WIDTH, encrypted_profile_size);
	std::println("{:<{}}{} bytes.", "Theoretical adaptive capacity of this cover image:", LABEL_WIDTH,
		carrier.payload_capacity);
	std::println("{:<{}}{} bytes (~{}KiB).", "Maximum stored payload size for this filename:", LABEL_WIDTH,
		max_stored_payload, max_stored_payload / BYTES_PER_KIBIBYTE);
	if (recommended_payload > 0) {
		std::println("{:<{}}{} bytes (~{}KiB, keeps 1KiB headroom).", "Recommended stored payload size:", LABEL_WIDTH,
			recommended_payload, recommended_payload / BYTES_PER_KIBIBYTE);
	}
	static_cast<void>(std::fflush(stdout));

	throw std::runtime_error(std::format(
		"Data File Size Error: Stored payload of {} bytes exceeds this cover image's capacity "
		"by {} byte{}.\n"
		"                      Use a smaller data file or a larger cover image.",
		stored_payload_size,
		bytes_over,
		bytes_over == 1 ? "" : "s"));
}

void concealRedditData(
	vBytes& png_vec,
	OpenInputFile& data_file,
	const std::string& data_filename) {
	constexpr std::size_t LARGE_COMPRESSED_PAYLOAD = 200uz * 1024;

	beginStep("Preparing cover image");
	const PngFormat source_format = readPngFormat(png_vec);
	RedditPngCarrier carrier = prepareRedditCarrier(png_vec);
	const PngFormat carrier_format{
		.width = carrier.width,
		.height = carrier.height,
		.bit_depth = 8,
		.color_type = 2,
	};
	endStep(describeCover(source_format, carrier_format, "metadata removed"));

	beginStep("Compressing data file");
	const std::string_view compressed_format = compressedFormatOf(data_file);
	const bool is_compressed_file = !compressed_format.empty();
	const SecureBytes compressed = compressRedditPayload(
		data_file.fd(),
		data_file.size(),
		is_compressed_file);
	endStep(describeCompression(data_file.size(), compressed.size(), compressed_format));

	beginStep("Checking cover capacity");
	const std::size_t encrypted_profile_size = computeRedditEncryptedProfileSize(
		compressed.size(),
		data_filename.size());
	if (encrypted_profile_size > carrier.payload_capacity) {
		failStep("does not fit.");
		reportRedditCapacityFailure(
			carrier,
			data_file.size(),
			compressed_format,
			compressed.size(),
			data_filename.size(),
			encrypted_profile_size);
	}
	endStep(std::format("{} of {} bytes ({}).",
		encrypted_profile_size,
		carrier.payload_capacity,
		percentText(encrypted_profile_size, carrier.payload_capacity)));
	if (const RedditCapacityLimits limits = redditCapacityLimits(carrier);
		compressed.size() > limits.recommended_compressed) {
		std::println(
			" Note: the stored payload ({} bytes) fits, but is above the recommended maximum of {} bytes\n"
			" for this cover image. It sits close to the carrier's capacity limit.",
			compressed.size(),
			limits.recommended_compressed);
	}

	beginStep("Encrypting");
	vBytes profile_vec = makeProfileTemplate(false);
	RecoveryKey recovery_key;
	encryptPreparedDataToProfile(
		recovery_key,
		profile_vec,
		compressed.span(),
		data_filename,
		false,
		carrier.payload_capacity);
	if (profile_vec.size() != encrypted_profile_size) {
		throw std::runtime_error(
			"Internal Error: Reddit encrypted profile size accounting mismatch.");
	}
	endStep(ENCRYPTION_RESULT);

	beginStep("Embedding");
	const bool wait_shown = showWaitNote(compressed.size() > LARGE_COMPRESSED_PAYLOAD);
	// Carrier version 4 is keyed through Argon2id and ChaCha20 from the 128-bit
	// KEY: checking the known carrier header must cost a memory-hard derivation
	// per candidate KEY.
	CarrierKey carrier_key;
	deriveCarrierKeyFromRecoveryKey(recovery_key, carrier_key);
	vBytes embedded_png = embedRedditPngPayload(carrier, carrier_key, profile_vec);
	endStepAfterWait(wait_shown, REDDIT_EMBED_RESULT);

	validateSizeLimit(embedded_png.size(), Option::Reddit, "Final output PNG");
	const OutputReport report{
		.embed_result = {},  // embedded above, in memory
		.write_result = std::format("{} ({} of Reddit's {} MiB upload size limit).",
			formatSizePrecise(embedded_png.size()),
			percentText(embedded_png.size(), REDDIT_UPLOAD_SIZE_LIMIT),
			REDDIT_UPLOAD_SIZE_LIMIT / (1024 * 1024)),
		.platforms = getRedditCompatiblePlatforms(embedded_png.size(), carrier.width, carrier.height),
	};
	writeOutputFile(embedded_png.size(), recovery_key, report, [&](int fd) {
		writeAllToFd(fd, embedded_png);
	});
}
} // namespace

void displayRedditCapacity(vBytes& png_vec, const fs::path& image_file_path) {
	const std::size_t minimum_single_frame_overhead =
		computeRedditEncryptedProfileSize(1, 1) - 1;
	const std::size_t maximum_single_frame_overhead =
		computeRedditEncryptedProfileSize(
			1,
			DATA_FILENAME_MAX_LENGTH) - 1;

	std::println("\nCover image: {} ({}).",
		printableFilename(image_file_path.filename().string()), formatSize(png_vec.size()));
	std::println("Capacity check for conceal -r (Reddit) mode only.\n");

	beginStep("Preparing cover image");
	const PngFormat source_format = readPngFormat(png_vec);
	const RedditPngCarrier carrier = prepareRedditCarrier(png_vec);
	const PngFormat carrier_format{
		.width = carrier.width,
		.height = carrier.height,
		.bit_depth = 8,
		.color_type = 2,
	};
	endStep(describeCover(source_format, carrier_format, "metadata removed"));

	beginStep("Measuring carrier capacity");
	endStep(std::format("{}, adaptive (1,15,4) matrix embedding.",
		bytesWithSize(carrier.payload_capacity)));

	beginStep("Calculating payload limits");
	endStep(describeRedditPayloadLimits(redditCapacityLimits(carrier)));

	std::println(
		"\nImportant:\n\n"
		"This is total encrypted carrier-envelope capacity, not a raw secret-file limit.\n\n"
		"PDVRDT compresses the input first. A file whose header shows an already-compressed format\n"
		"(ZIP, gzip, JPEG, PNG, MP4, etc.) is stored in a level-0 zlib stream; compression may\n"
		"shrink or slightly expand other inputs.\n\n"
		"For a payload contained in one encryption frame, filename, encryption and recovery metadata\n"
		"consume {} to {} bytes; larger payloads add framing overhead.\n\n"
		"Do not target the theoretical exact limit. When capacity permits, keep the stored payload at least\n"
		"1KiB below the conservative maximum shown above. The final conceal -r size check is authoritative.\n"
		"The final embedded PNG size can differ from the cover image and must remain within 20MiB.\n",
		minimum_single_frame_overhead,
		maximum_single_frame_overhead);
}

void concealData(
	vBytes& png_vec,
	Option option,
	const fs::path& image_file_path,
	const fs::path& data_file_path) {

	const bool is_mastodon = (option == Option::Mastodon);
	const bool is_reddit = (option == Option::Reddit);

	OpenInputFile data_file = openInputFile(data_file_path, FileTypeCheck::data_file);
	const std::size_t data_file_size = data_file.size();
	if (is_reddit) {
		validateRedditPayloadInputSize(data_file_size);
	}

	std::string data_filename = data_file_path.filename().string();
	validateDataFilename(data_file_path, data_filename);

	printConcealHeader(image_file_path, png_vec.size(), data_filename, data_file_size);

	if (is_reddit) {
		concealRedditData(png_vec, data_file, data_filename);
		return;
	}

	beginStep("Preparing cover image");
	const PngFormat source_format = readPngFormat(png_vec);
	const bool has_bad_dims = optimizeImage(png_vec);
	if (is_mastodon) {
		prepareImageForMastodonEmbedding(png_vec);
	}
	endStep(describeCover(source_format, readPngFormat(png_vec), is_mastodon
		? "color metadata kept (iCCP/sRGB removed), other metadata removed"
		: "color metadata kept, other metadata removed"));

	const std::string_view compressed_format = compressedFormatOf(data_file);
	const bool is_compressed = !compressed_format.empty();

	// Fail before spending Argon2 + encryption on a payload that provably cannot
	// fit. Sound only for already-compressed inputs: those are *stored*, not
	// deflated (see StoredZlibWriter in compression.cpp), so the profile is
	// guaranteed to be at least as large as the input. A compressible payload may
	// still shrink under the limit, so it is checked the slow way as it streams.
	// (For default mode the payload budget is the profile budget itself.)
	if (is_compressed &&
		data_file_size > (is_mastodon
			? maximumMastodonCompressedProfileSize(png_vec.size())
			: maximumProfileSizeForEncryption(png_vec.size(), option))) {
		const std::size_t output_limit = sizeLimitForOption(option).first;
		throw std::runtime_error(std::format(
			"File Size Error: \"{}\" is {} bytes and its header shows {} data, which is already "
			"compressed, so it cannot be reduced to fit {}.",
			data_filename, data_file_size, compressed_format,
			is_mastodon
				? std::string("the Mastodon size limit")
				: std::format("the {} GiB output size limit", output_limit / (1024 * 1024 * 1024))));
	}

	if (!is_mastodon) {
		concealDefaultStreamed(png_vec, data_file, data_filename, compressed_format, option, has_bad_dims);
		return;
	}

	// Mastodon's profile is at most 16 MiB and is wrapped in a zlib stream that
	// needs its final length, so it is built in memory first.
	vBytes profile_vec = makeProfileTemplate(true);
	const std::size_t max_profile_size = maximumProfileSizeForEncryption(png_vec.size(), option);
	// Owns the KEY from generation until the write finishes or any post-encrypt
	// path throws; encryptCompressedFileToProfile() fills it in place.
	RecoveryKey recovery_key;
	// Compression and encryption are one streamed pass here (each compressed
	// piece is encrypted as it is produced), reported as their two steps.
	beginStep("Compressing data file");
	const bool wait_shown = showWaitNote(data_file_size > LARGE_FILE_SIZE);
	const std::size_t stored_size = encryptCompressedFileToProfile(
		recovery_key,
		profile_vec,
		data_file.fd(),
		data_file.size(),
		data_filename,
		is_compressed,
		true,
		max_profile_size
	);
	endStepAfterWait(wait_shown, describeCompression(data_file_size, stored_size, compressed_format));
	beginStep("Encrypting");
	endStep(ENCRYPTION_RESULT);

	writeMastodonOutput(png_vec, profile_vec, option, has_bad_dims, recovery_key);
}
