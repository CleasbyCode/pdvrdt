#include "conceal.h"
#include "encryption.h"
#include "image.h"
#include "io_utils.h"
#include "pin_codec.h"
#include "png_utils.h"
#include "reddit_steg.h"
#include "compression.h"

#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <format>
#include <print>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

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

[[nodiscard]] bool isLikelyCompressedInputFile(const fs::path& path) {
	// Formats whose contents are already entropy-coded, so a deflate pass gains
	// ~0%. Executables (.exe) are deliberately absent: they typically deflate by
	// 30-50%, so storing them wasted capacity.
	return hasFileExtension(path, {
		// archives and compressed streams
		".zip", ".jar", ".apk", ".rar", ".7z", ".bz2", ".gz", ".tgz", ".xz", ".txz", ".lz",
		".lz4", ".zst", ".cab", ".rpm", ".deb",
		// zip-based documents
		".docx", ".xlsx", ".pptx", ".odt", ".ods", ".odp", ".epub",
		// audio / video
		".mp4", ".m4v", ".mov", ".mkv", ".webm", ".mp3", ".m4a", ".aac", ".ogg", ".opus", ".flac",
		// images
		".jpg", ".jpeg", ".jfif", ".png", ".webp", ".gif", ".avif", ".heic", ".heif", ".jxl"
	});
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

// A closed stdout pipe would normally terminate the process with SIGPIPE before
// writeOutputFile() can remove its newly-created PNG.  Ignore SIGPIPE only for
// the short, checked reporting transaction so stream failures become ordinary
// errors and take the same cleanup path as file-write failures.
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
	const RecoveryPin& pin) {
	SigpipeIgnoreGuard sigpipe_guard;
	// Verify all earlier informational output reached the stream before adding
	// the success report.  stdio error indicators are sticky, so this also
	// catches failures from the compatibility report above.
	flushStdoutOrThrow();

	KeyDisplay key_text{};
	ScopedWipe key_text_wiper{key_text};
	encodeKeyDisplay(pin, key_text);
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
	sigpipe_guard.finish();
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
	if (option == Option::Reddit) {
		return { "Reddit. (Only share this \"file-embedded\" PNG image on Reddit)." };
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

template <typename Writer>
void writeOutputFile(std::size_t output_size, const RecoveryPin& pin, Writer&& writer) {
	constexpr std::size_t MAX_NAME_ATTEMPTS = 2048;

	// Unnamed until published, so an interrupted write never leaves an image
	// whose KEY was not shown (see OutputFile).
	OutputFile output_file = OutputFile::createInCurrentDirectory();
	std::forward<Writer>(writer)(output_file.fd());
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
		reportOutputOrThrow(output_path, output_size, pin);
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
}

[[nodiscard]] vBytes makeProfileTemplate(bool is_mastodon) {
	if (is_mastodon) {
		return vBytes(MASTODON_PROFILE.begin(), MASTODON_PROFILE.end());
	}
	return vBytes(DEFAULT_PROFILE.begin(), DEFAULT_PROFILE.end());
}

void printPlatformCompatibility(Option option, std::size_t output_size, bool has_bad_dims, bool twitter_iccp_compatible) {
	const auto platforms = getCompatiblePlatforms(option, output_size, has_bad_dims, twitter_iccp_compatible);

	std::println("\nPlatform compatibility for output image:-\n");
	for (const auto& platform : platforms) {
		std::println(" ✓ {}", platform);
	}
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
	const vBytes& profile_vec,
	Option option,
	bool has_bad_dims,
	const RecoveryPin& pin) {

	constexpr std::size_t
		MASTODON_INSERT_INDEX     = 0x21,
		TWITTER_ICCP_MAX_CHUNK_SIZE = 10uz * 1024,
		TWITTER_IMAGE_MAX_SIZE      = 5uz * 1024 * 1024,
		ICCP_SIZE_DIFF              = MASTODON_ICCP_PREFIX_BYTES;

	if (MASTODON_INSERT_INDEX > png_vec.size()) {
		throw std::runtime_error("Image File Error: Invalid PNG insertion point for iCCP chunk.");
	}
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
	printPlatformCompatibility(option, output_size, has_bad_dims, twitter_iccp_compatible);

	writeOutputFile(output_size, pin, [&](int fd) {
		writeAllToFd(fd, std::span<const Byte>(png_vec.data(), MASTODON_INSERT_INDEX));
		ChunkWriter chunk(fd, TYPE_ICCP, mastodon_chunk_data_size);
		chunk.write(PDVRDT_ICCP_PREFIX);
		zlibStoreSpan(profile_vec, [&chunk](std::span<const Byte> piece) { chunk.write(piece); });
		chunk.finish();
		writePngTailFrom(fd, png_vec, MASTODON_INSERT_INDEX);
	});
}

void writeDefaultOutput(
	const vBytes& png_vec,
	const vBytes& profile_vec,
	Option option,
	bool has_bad_dims,
	const RecoveryPin& pin) {

	constexpr std::size_t
		DEFAULT_INSERT_DIFF = PNG_CHUNK_OVERHEAD,  // the zero-length IEND
		IDAT_SIZE_DIFF = DEFAULT_IDAT_PREFIX_BYTES;

	if (png_vec.size() < DEFAULT_INSERT_DIFF) {
		throw std::runtime_error("Image File Error: Invalid PNG insertion point for IDAT chunk.");
	}

	const std::size_t insert_index = png_vec.size() - DEFAULT_INSERT_DIFF;
	const std::size_t output_size = checkedAddSize(
		png_vec.size(),
		checkedChunkTotalSize(profile_vec.size(), IDAT_SIZE_DIFF),
		"File Size Error: Final output size overflow."
	);

	validateSizeLimit(output_size, option, "Final output PNG");
	printPlatformCompatibility(option, output_size, has_bad_dims, false);

	writeOutputFile(output_size, pin, [&](int fd) {
		writeAllToFd(fd, std::span<const Byte>(png_vec.data(), insert_index));
		ChunkWriter chunk(fd, TYPE_IDAT,
			checkedChunkDataSize(profile_vec.size(), IDAT_SIZE_DIFF));
		chunk.write(PDVRDT_IDAT_PREFIX);
		chunk.write(profile_vec);
		chunk.finish();
		writePngTailFrom(fd, png_vec, insert_index);
	});
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

void printRedditCoverSummary(
	const RedditPngCarrier& carrier,
	std::size_t source_cover_size) {

	std::println(
		"Cover Image: {}KiB, {}x{}, Non-interlaced 8-bit RGB PNG, Adaptive (1,15,4) matrix embedding.\n",
		source_cover_size / BYTES_PER_KIBIBYTE,
		carrier.width,
		carrier.height);
}

void printRedditCapacityLimits(
	const RedditPngCarrier& carrier,
	const RedditCapacityLimits& limits) {

	constexpr int CAPACITY_LABEL_WIDTH = 71;
	std::println(
		"{:<{}}{} bytes (~{}KiB).",
		"Theoretical adaptive capacity limit for this cover image:",
		CAPACITY_LABEL_WIDTH,
		carrier.payload_capacity,
		carrier.payload_capacity / BYTES_PER_KIBIBYTE);
	std::println(
		"{:<{}}{} bytes (~{}KiB).",
		"Conservative maximum stored payload capacity with a 20-byte filename:",
		CAPACITY_LABEL_WIDTH,
		limits.conservative_compressed,
		limits.conservative_compressed / BYTES_PER_KIBIBYTE);
	std::println(
		"{:<{}}{} bytes (~{}KiB).",
		"Recommended  maximum stored payload capacity with a 20-byte filename:",
		CAPACITY_LABEL_WIDTH,
		limits.recommended_compressed,
		limits.recommended_compressed / BYTES_PER_KIBIBYTE);
}

// The payload does not fit this cover with this filename: a hard limit, so
// report the real figures for the supplied filename rather than the generic
// 20-byte-filename guidance that capsize prints.
[[noreturn]] void reportRedditCapacityFailure(
	const RedditPngCarrier& carrier,
	std::size_t source_cover_size,
	std::size_t source_data_size,
	bool is_compressed_file,
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
	const std::string storage_note = is_compressed_file
		? std::format("level-0 zlib stream of the {}-byte file", source_data_size)
		: stored_payload_size < source_data_size
			? std::format("compressed by pdvrdt from {} bytes", source_data_size)
			: std::format("zlib stream of the {}-byte file; compression did not reduce it", source_data_size);

	std::print("\n");
	printRedditCoverSummary(carrier, source_cover_size);
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
	const fs::path& data_file_path,
	const std::string& data_filename) {
	constexpr std::size_t LARGE_COMPRESSED_PAYLOAD = 200uz * 1024;

	const std::size_t source_cover_size = png_vec.size();
	RedditPngCarrier carrier = prepareRedditCarrier(png_vec);

	const bool is_compressed_file = isLikelyCompressedInputFile(data_file_path);
	const SecureBytes compressed = compressRedditPayload(
		data_file.fd(),
		data_file.size(),
		is_compressed_file);

	const std::size_t encrypted_profile_size = computeRedditEncryptedProfileSize(
		compressed.size(),
		data_filename.size());
	if (encrypted_profile_size > carrier.payload_capacity) {
		reportRedditCapacityFailure(
			carrier,
			source_cover_size,
			data_file.size(),
			is_compressed_file,
			compressed.size(),
			data_filename.size(),
			encrypted_profile_size);
	}
	if (const RedditCapacityLimits limits = redditCapacityLimits(carrier);
		compressed.size() > limits.recommended_compressed) {
		std::println(
			"\nNote: the stored payload ({} bytes) fits, but is above the recommended maximum of {} bytes\n"
			"for this cover image. It sits close to the carrier's capacity limit.",
			compressed.size(),
			limits.recommended_compressed);
	}
	if (compressed.size() > LARGE_COMPRESSED_PAYLOAD) {
		std::println(
			"\nPlease wait. Larger payloads will take longer to complete this process.");
	}

	vBytes profile_vec = makeProfileTemplate(false);
	RecoveryPin pin;
	encryptPreparedDataToProfile(
		pin,
		profile_vec,
		compressed.span(),
		data_filename,
		false,
		carrier.payload_capacity);
	if (profile_vec.size() != encrypted_profile_size) {
		throw std::runtime_error(
			"Internal Error: Reddit encrypted profile size accounting mismatch.");
	}

	// Carrier version 4 is keyed through Argon2id and ChaCha20 from the 128-bit
	// KEY: checking the known carrier header must cost a memory-hard derivation
	// per candidate KEY.
	CarrierKeys carrier_keys;
	deriveCarrierKeysFromPin(pin, carrier_keys);
	vBytes embedded_png = embedRedditPngPayload(carrier, carrier_keys, profile_vec);
	validateSizeLimit(embedded_png.size(), Option::Reddit, "Final output PNG");
	printPlatformCompatibility(Option::Reddit, embedded_png.size(), false, false);

	writeOutputFile(embedded_png.size(), pin, [&](int fd) {
		writeAllToFd(fd, embedded_png);
	});
}
} // namespace

void displayRedditCapacity(vBytes& png_vec) {
	const std::size_t source_cover_size = png_vec.size();
	RedditPngCarrier carrier = prepareRedditCarrier(png_vec);
	const RedditCapacityLimits limits = redditCapacityLimits(carrier);
	const std::size_t minimum_single_frame_overhead =
		computeRedditEncryptedProfileSize(1, 1) - 1;
	const std::size_t maximum_single_frame_overhead =
		computeRedditEncryptedProfileSize(
			1,
			DATA_FILENAME_MAX_LENGTH) - 1;

	std::println("\nReddit capacity check for conceal -r mode only.\n");
	printRedditCoverSummary(carrier, source_cover_size);
	printRedditCapacityLimits(carrier, limits);

	std::println(
		"\nImportant:\n\n"
		"This is total encrypted carrier-envelope capacity, not a raw secret-file limit.\n\n"
		"PDVRDT compresses the input first. Recognized already-compressed file types are stored\n"
		"in a level-0 zlib stream; compression may shrink or slightly expand other inputs.\n\n"
		"For a payload contained in one encryption frame, filename, encryption and recovery metadata\n"
		"consume {} to {} bytes; larger payloads add framing overhead.\n\n"
		"Do not target the theoretical exact limit. When capacity permits, keep the stored payload at least\n"
		"1KiB below the conservative maximum shown above. The final conceal -r size check is authoritative.\n"
		"The final embedded PNG size can differ from the cover image and must remain within 20MiB.\n",
		minimum_single_frame_overhead,
		maximum_single_frame_overhead);
}

void concealData(vBytes& png_vec, Option option, const fs::path& data_file_path) {
	constexpr std::size_t LARGE_FILE_SIZE = 300uz * 1024 * 1024;

	const bool is_mastodon = (option == Option::Mastodon);
	const bool is_reddit = (option == Option::Reddit);

	OpenInputFile data_file = openInputFile(data_file_path, FileTypeCheck::data_file);
	const std::size_t data_file_size = data_file.size();
	if (is_reddit) {
		validateRedditPayloadInputSize(data_file_size);
	}

	std::string data_filename = data_file_path.filename().string();
	validateDataFilename(data_file_path, data_filename);

	if (data_file_size > LARGE_FILE_SIZE) {
		std::println("\nPlease wait. Larger files will take longer to complete this process.");
	}

	if (is_reddit) {
		concealRedditData(png_vec, data_file, data_file_path, data_filename);
		return;
	}

	const bool has_bad_dims = optimizeImage(png_vec);
	if (is_mastodon) {
		prepareImageForMastodonEmbedding(png_vec);
	}

	const bool is_compressed = isLikelyCompressedInputFile(data_file_path);

	// Fail before spending Argon2 + encryption on a payload that provably cannot
	// fit. Sound only for already-compressed inputs: those are *stored*, not
	// deflated (see StoredZlibWriter in compression.cpp), so the profile is
	// guaranteed to be at least as large as the input. A compressible payload may
	// still shrink under the limit, so it is checked the slow way as it streams.
	if (is_mastodon && is_compressed &&
		data_file_size > maximumMastodonCompressedProfileSize(png_vec.size())) {
		throw std::runtime_error(std::format(
			"File Size Error: \"{}\" is {} bytes and is already in a compressed format, "
			"so it cannot be reduced to fit the Mastodon size limit.",
			data_filename, data_file_size));
	}

	vBytes profile_vec = makeProfileTemplate(is_mastodon);
	const std::size_t max_profile_size = maximumProfileSizeForEncryption(png_vec.size(), option);
	// Owns the KEY from generation until the write finishes or any post-encrypt
	// path throws; encryptCompressedFileToProfile() fills it in place.
	RecoveryPin pin;
	encryptCompressedFileToProfile(
		pin,
		profile_vec,
		data_file.fd(),
		data_file.size(),
		data_filename,
		is_compressed,
		is_mastodon,
		max_profile_size
	);

	if (is_mastodon) {
		writeMastodonOutput(png_vec, profile_vec, option, has_bad_dims, pin);
	} else {
		writeDefaultOutput(png_vec, profile_vec, option, has_bad_dims, pin);
	}
}
