#include "conceal.h"
#include "encryption.h"
#include "image.h"
#include "io_utils.h"
#include "png_utils.h"
#include "reddit_steg.h"
#include "compression.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <format>
#include <initializer_list>
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
	PlatformLimits{ "Flickr",    200ULL * 1024 * 1024, false },
	PlatformLimits{ "ImgBB",      32ULL * 1024 * 1024, false },
	PlatformLimits{ "PostImage",  32ULL * 1024 * 1024, false },
	PlatformLimits{ "ImgPile",     8ULL * 1024 * 1024, false },
	PlatformLimits{ "X-Twitter",   5ULL * 1024 * 1024, true  },
};

constexpr std::size_t
	PNG_CHUNK_OVERHEAD               = 12,
	DEFAULT_IDAT_PREFIX_BYTES        = 3,
	MASTODON_ICCP_PREFIX_BYTES       = 5,
	DATA_FILENAME_MAX_LENGTH         = 20,
	// PNG spec 5.3: a chunk's length field "must not exceed 2^31-1", not the
	// 2^32-1 the four-byte field could hold. The output size limits happen to keep
	// every chunk we emit under this today, but by only ~80 bytes -- so enforce it
	// directly rather than leaving spec compliance resting on that margin.
	PNG_MAX_CHUNK_DATA_SIZE          = 0x7FFFFFFFULL;

[[nodiscard]] std::pair<std::size_t, std::string_view> sizeLimitForOption(Option option) {
	constexpr std::size_t
		MAX_SIZE_DEFAULT  = 2ULL * 1024 * 1024 * 1024,
		MAX_SIZE_MASTODON = 16ULL * 1024 * 1024;

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
		throw std::runtime_error("Data File Error: For compatibility requirements, length of data filename must not exceed 20 characters.");
	}
}

[[nodiscard]] bool isLikelyCompressedInputFile(const fs::path& path) {
	return hasFileExtension(path, {
		".zip", ".jar", ".rar", ".7z", ".bz2", ".gz", ".xz", ".lz", ".lz4", ".cab", ".rpm", ".deb",
		".mp4", ".mp3", ".exe", ".jpg", ".jpeg", ".jfif", ".png", ".webp", ".gif", ".ogg", ".flac"
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

[[nodiscard]] std::uint32_t checkedChunkDataSizeFromParts(std::initializer_list<std::span<const Byte>> chunk_data_parts) {
	std::size_t total = 0;
	for (const auto part : chunk_data_parts) {
		if (part.size() > PNG_MAX_CHUNK_DATA_SIZE - total) {
			throw std::runtime_error("PNG Error: Chunk payload exceeds PNG chunk size limit.");
		}
		total += part.size();
	}
	return static_cast<std::uint32_t>(total);
}

struct OutputFileHandle {
	fs::path path{};
	int fd{-1};
};

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
			"Output Error: Unable to reliably report the output filename and recovery PIN: {}",
			ec.message()));
	}
}

void reportOutputOrThrow(
	const fs::path& output_path,
	std::size_t output_size,
	const std::uint64_t& pin) {
	SigpipeIgnoreGuard sigpipe_guard;
	// Verify all earlier informational output reached the stream before adding
	// the success report.  stdio error indicators are sticky, so this also
	// catches failures from the compatibility report above.
	flushStdoutOrThrow();

	std::string report = std::format(
		"\nSaved \"file-embedded\" PNG image: {} ({} bytes).\n"
		"\nRecovery PIN: [***{}***]\n\nImportant: Keep your PIN safe, so that you can extract the hidden file.\n\nComplete!\n\n",
		output_path.string(), output_size, pin);
	ScopedWipe report_wiper{report};

	errno = 0;
	const std::size_t written = std::fwrite(report.data(), 1, report.size(), stdout);
	const int write_errno = errno;
	if (written != report.size() || std::ferror(stdout) != 0) {
		const std::error_code ec(write_errno != 0 ? write_errno : EIO, std::generic_category());
		throw std::runtime_error(std::format(
			"Output Error: Unable to reliably report the output filename and recovery PIN: {}",
			ec.message()));
	}
	flushStdoutOrThrow();
	sigpipe_guard.finish();
}

[[nodiscard]] OutputFileHandle createUniqueOutputFile() {
	constexpr std::size_t MAX_ATTEMPTS = 2048;

	for (std::size_t i = 0; i < MAX_ATTEMPTS; ++i) {
		const uint32_t rand_num = 100000 + randombytes_uniform(900000);
		const fs::path candidate = std::format("prdt_{}.png", rand_num);

		int flags = O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC;
#ifdef O_NOFOLLOW
		flags |= O_NOFOLLOW;
#endif
		const int fd = ::open(candidate.c_str(), flags, S_IRUSR | S_IWUSR);
		if (fd >= 0) {
			return OutputFileHandle{ .path = candidate, .fd = fd };
		}

		if (errno == EEXIST) {
			continue;
		}

		const std::error_code ec(errno, std::generic_category());
		throw std::runtime_error(std::format("Write Error: Unable to create output file: {}", ec.message()));
	}
	throw std::runtime_error("Write Error: Unable to allocate output filename.");
}

void writeUint32OrThrow(int fd, std::uint32_t value) {
	std::array<Byte, 4> bytes{};
	updateValue(bytes, 0, value);
	writeAllToFd(fd, bytes);
}

void writeChunkFromParts(int fd, std::span<const Byte> chunk_type, std::initializer_list<std::span<const Byte>> chunk_data_parts) {
	if (chunk_type.size() != 4) {
		throw std::invalid_argument("PNG Error: Invalid chunk type size.");
	}

	const std::uint32_t chunk_data_size = checkedChunkDataSizeFromParts(chunk_data_parts);
	writeUint32OrThrow(fd, chunk_data_size);
	writeAllToFd(fd, chunk_type);

	std::uint32_t crc = pdvrdtCrc32Update(0, chunk_type);

	for (const auto part : chunk_data_parts) {
		writeAllToFd(fd, part);
		crc = pdvrdtCrc32Update(crc, part);
	}

	writeUint32OrThrow(fd, crc);
}

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
void writeOutputFile(std::size_t output_size, std::uint64_t& pin, Writer&& writer) {
	// Wipe the PIN on every exit (success or throw after encryption).
	ScopedWipe pin_wiper{pin};

	OutputFileHandle output_file = createUniqueOutputFile();

	try {
		std::forward<Writer>(writer)(output_file.fd);
		verifyFdSize(output_file.fd, output_size);
		// Durability before the success report: the PIN is printed exactly once and
		// exists nowhere else, so an image still sitting in the page cache when the
		// machine loses power would be unrecoverable even though the user was told
		// the operation completed.
		fsyncFdOrThrow(output_file.fd);
		closeFdOrThrow(output_file.fd);
		fsyncParentDirectoryNoThrow(output_file.path);
		reportOutputOrThrow(output_file.path, output_size, pin);
	} catch (...) {
		// Deliberate, including when reportOutputOrThrow() fails *after* a
		// complete image was written and closed: the PIN exists only in this
		// process and is wiped on the way out, so an image whose PIN never
		// reached the user can never be recovered by anyone. Leaving it behind
		// would only be a confusing, permanently opaque file. Do not "fix" this
		// into keeping the output.
		closeFdNoThrow(output_file.fd);
		cleanupPathNoThrow(output_file.path);
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

// iCCP requires its profile to be a zlib stream, so the encrypted profile has to
// be wrapped -- but it must not be *deflated*. The profile is ciphertext, which
// is incompressible by construction: level 6 measures ~190 ms per 12 MiB and
// produces slightly more output than storing it (~2 ms). Store it.
[[nodiscard]] vBytes storeMastodonProfile(
	const vBytes& profile_vec,
	std::size_t max_compressed_size) {

	vBytes compressed_profile;
	zlibStoreSpan(
		std::span<const Byte>(profile_vec.data(), profile_vec.size()),
		[&](std::span<const Byte> chunk) {
			if (chunk.size() > max_compressed_size - compressed_profile.size()) {
				throw std::runtime_error(
					"File Size Error: Compressed profile exceeds the Mastodon output size limit.");
			}
			appendBytes(compressed_profile, chunk,
				"File Size Error: Compressed profile size overflow.");
		}
	);
	return compressed_profile;
}

void writeMastodonOutput(
	const vBytes& png_vec,
	const vBytes& profile_vec,
	Option option,
	bool has_bad_dims,
	std::uint64_t& pin) {

	constexpr std::size_t
		MASTODON_INSERT_INDEX     = 0x21,
		TWITTER_ICCP_MAX_CHUNK_SIZE = 10ULL * 1024,
		TWITTER_IMAGE_MAX_SIZE      = 5ULL * 1024 * 1024,
		ICCP_SIZE_DIFF              = MASTODON_ICCP_PREFIX_BYTES;
	constexpr auto TYPE_ICCP = std::to_array<Byte>({ 0x69, 0x43, 0x43, 0x50 });

	if (MASTODON_INSERT_INDEX > png_vec.size()) {
		throw std::runtime_error("Image File Error: Invalid PNG insertion point for iCCP chunk.");
	}
	const std::size_t max_compressed_profile_size = maximumMastodonCompressedProfileSize(png_vec.size());
	const vBytes compressed_profile = storeMastodonProfile(
		profile_vec, max_compressed_profile_size);
	const std::uint32_t mastodon_chunk_data_size = checkedChunkDataSize(compressed_profile.size(), ICCP_SIZE_DIFF);

	const std::size_t output_size = checkedAddSize(
		png_vec.size(),
		checkedChunkTotalSize(compressed_profile.size(), ICCP_SIZE_DIFF),
		"File Size Error: Final output size overflow."
	);
	const bool twitter_iccp_compatible =
		output_size <= TWITTER_IMAGE_MAX_SIZE &&
		mastodon_chunk_data_size <= TWITTER_ICCP_MAX_CHUNK_SIZE;

	validateSizeLimit(output_size, option, "Final output PNG");
	printPlatformCompatibility(option, output_size, has_bad_dims, twitter_iccp_compatible);

	writeOutputFile(output_size, pin, [&](int fd) {
		writeAllToFd(fd, std::span<const Byte>(png_vec.data(), MASTODON_INSERT_INDEX));
		writeChunkFromParts(fd, TYPE_ICCP, {
			std::span<const Byte>(PDVRDT_ICCP_PREFIX),
			std::span<const Byte>(compressed_profile.data(), compressed_profile.size())
		});
		writePngTailFrom(fd, png_vec, MASTODON_INSERT_INDEX);
	});
}

void writeDefaultOutput(
	const vBytes& png_vec,
	const vBytes& profile_vec,
	Option option,
	bool has_bad_dims,
	std::uint64_t& pin) {

	constexpr std::size_t
		DEFAULT_INSERT_DIFF = 12,
		IDAT_SIZE_DIFF = DEFAULT_IDAT_PREFIX_BYTES;
	constexpr auto TYPE_IDAT = std::to_array<Byte>({ 0x49, 0x44, 0x41, 0x54 });

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
		writeChunkFromParts(fd, TYPE_IDAT, {
			std::span<const Byte>(PDVRDT_IDAT_PREFIX),
			std::span<const Byte>(profile_vec.data(), profile_vec.size())
		});
		writePngTailFrom(fd, png_vec, insert_index);
	});
}

void validateRedditPreliminaryLimits(
	std::size_t cover_size,
	std::size_t payload_size) {

	const bool cover_too_large = cover_size > REDDIT_UPLOAD_SIZE_LIMIT;
	const bool payload_too_large = payload_size > REDDIT_UPLOAD_SIZE_LIMIT;
	if (cover_too_large && payload_too_large) {
		throw std::runtime_error(
			"File Size Error: Cover image and payload file exceed Reddit's 20 MiB upload size limit.");
	}
	if (cover_too_large) {
		throw std::runtime_error(
			"Image File Size Error: Cover image exceeds Reddit's 20 MiB upload size limit.");
	}
	if (payload_too_large) {
		throw std::runtime_error(
			"Data File Size Error: Payload file exceeds Reddit's 20 MiB upload size limit.");
	}
}

[[nodiscard]] vBytes compressRedditPayload(
	int data_fd,
	std::size_t data_file_size,
	bool is_compressed_file) {

	vBytes compressed;
	zlibDeflateFd(
		data_fd,
		data_file_size,
		is_compressed_file,
		[&](std::span<const Byte> chunk) {
			appendBytes(
				compressed,
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
		.recommended_compressed = conservative_compressed > 1000
			? conservative_compressed - 1000
			: 0,
	};
}

void printRedditCoverSummary(
	const RedditPngCarrier& carrier,
	std::size_t source_cover_size) {

	std::println(
		"Cover Image: {}KB, {}x{}, Non-interlaced 8-bit RGB PNG, Adaptive (1,15,4) matrix embedding.\n",
		source_cover_size / 1000,
		carrier.width,
		carrier.height);
}

void printRedditCapacityLimits(
	const RedditPngCarrier& carrier,
	const RedditCapacityLimits& limits) {

	constexpr int CAPACITY_LABEL_WIDTH = 71;
	std::println(
		"{:<{}}{} bytes (~{}KB).",
		"Theoretical adaptive capacity limit for this cover image:",
		CAPACITY_LABEL_WIDTH,
		carrier.payload_capacity,
		carrier.payload_capacity / 1000);
	std::println(
		"{:<{}}{} bytes (~{}KB).",
		"Conservative maximum compressed capacity with a 20-character filename:",
		CAPACITY_LABEL_WIDTH,
		limits.conservative_compressed,
		limits.conservative_compressed / 1000);
	std::println(
		"{:<{}}{} bytes (~{}KB).",
		"Recommended  maximum compressed capacity with a 20-character filename:",
		CAPACITY_LABEL_WIDTH,
		limits.recommended_compressed,
		limits.recommended_compressed / 1000);
}

[[noreturn]] void reportRedditCapacityFailure(
	const RedditPngCarrier& carrier,
	std::size_t source_cover_size,
	std::size_t compressed_size,
	std::size_t filename_length) {

	const RedditCapacityLimits limits = redditCapacityLimits(carrier);
	// The limit concealRedditData() actually enforced is the one for *this*
	// payload's filename, which is usually shorter than the 20-character worst
	// case the summary above quotes -- and it is a full 1000 bytes above the
	// recommended figure. Reporting the recommendation as though it were the
	// threshold told users to shed ~4% more than they had to.
	const std::size_t enforced_maximum = maxRedditCompressedPayload(
		carrier.payload_capacity,
		filename_length);

	std::print("\n");
	printRedditCoverSummary(carrier, source_cover_size);
	std::println(
		"Compressed data file (payload) size: {} bytes ({}KB).\n",
		compressed_size,
		compressed_size / 1000);
	printRedditCapacityLimits(carrier, limits);
	static_cast<void>(std::fflush(stdout));

	throw std::runtime_error(std::format(
		"Data File Size Error: \n\n"
		"Compressed payload size of {} bytes ({}KB) exceeds this cover image's maximum of "
		"{} bytes (~{}KB) for a {}-character filename.\n"
		"Where capacity permits, leave at least 1KB of headroom below that ({} bytes).",
		compressed_size,
		compressed_size / 1000,
		enforced_maximum,
		enforced_maximum / 1000,
		filename_length,
		limits.recommended_compressed));
}

void concealRedditData(
	vBytes& png_vec,
	OpenInputFile& data_file,
	const fs::path& data_file_path,
	const std::string& data_filename) {
	constexpr std::size_t LARGE_COMPRESSED_PAYLOAD = 200ULL * 1024;

	const std::size_t source_cover_size = png_vec.size();
	RedditPngCarrier carrier = prepareRedditCarrier(png_vec);

	const bool is_compressed_file = isLikelyCompressedInputFile(data_file_path);
	vBytes compressed = compressRedditPayload(
		data_file.fd(),
		data_file.size(),
		is_compressed_file);
	ScopedWipe compressed_wiper{compressed};

	const std::size_t encrypted_profile_size = computeRedditEncryptedProfileSize(
		compressed.size(),
		data_filename.size());
	if (encrypted_profile_size > carrier.payload_capacity) {
		reportRedditCapacityFailure(
			carrier,
			source_cover_size,
			compressed.size(),
			data_filename.size());
	}
	if (compressed.size() > LARGE_COMPRESSED_PAYLOAD) {
		std::println(
			"\nPlease wait. Larger payloads will take longer to complete this process.");
	}

	vBytes profile_vec = makeProfileTemplate(false);
	SensitiveU64 pin;
	encryptPreparedDataToProfile(
		pin,
		profile_vec,
		compressed,
		data_filename,
		false,
		carrier.payload_capacity);
	if (profile_vec.size() != encrypted_profile_size) {
		throw std::runtime_error(
			"Internal Error: Reddit encrypted profile size accounting mismatch.");
	}

	// The carrier's sample positions are keyed by the PIN that was just minted,
	// so an image only reveals that it carries anything to whoever holds it.
	vBytes embedded_png =
		embedRedditPngPayload(carrier, deriveCarrierKeyFromPin(pin), profile_vec);
	validateSizeLimit(embedded_png.size(), Option::Reddit, "Final output PNG");
	printPlatformCompatibility(Option::Reddit, embedded_png.size(), false, false);

	writeOutputFile(embedded_png.size(), pin.value, [&](int fd) {
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
		"Do not target the theoretical exact limit. When capacity permits, keep the compressed payload at least\n"
		"1KB below the conservative maximum shown above. The final conceal -r size check is authoritative.\n"
		"The final embedded PNG size can differ from the cover image and must remain within 20MB.\n",
		minimum_single_frame_overhead,
		maximum_single_frame_overhead);
}

void concealData(vBytes& png_vec, Option option, const fs::path& data_file_path) {
	constexpr std::size_t LARGE_FILE_SIZE = 300ULL * 1024 * 1024;

	const bool is_mastodon = (option == Option::Mastodon);
	const bool is_reddit = (option == Option::Reddit);

	OpenInputFile data_file = openInputFile(data_file_path, FileTypeCheck::data_file);
	const std::size_t data_file_size = data_file.size();
	if (is_reddit) {
		validateRedditPreliminaryLimits(png_vec.size(), data_file_size);
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
	// deflated (see payloadLevels in compression.cpp), so the profile is
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
	// Owns the PIN from generation until the write finishes or any post-encrypt
	// path throws; encryptCompressedFileToProfile() fills it in place.
	SensitiveU64 pin;
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
		writeMastodonOutput(png_vec, profile_vec, option, has_bad_dims, pin.value);
	} else {
		writeDefaultOutput(png_vec, profile_vec, option, has_bad_dims, pin.value);
	}
}
