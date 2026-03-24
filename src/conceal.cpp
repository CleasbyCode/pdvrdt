#include "conceal.h"
#include "encryption.h"
#include "image.h"
#include "io_utils.h"
#include "png_utils.h"
#include "compression.h"

#include <zlib.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <format>
#include <functional>
#include <initializer_list>
#include <limits>
#include <print>
#include <span>
#include <stdexcept>
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

[[nodiscard]] std::pair<std::size_t, std::string_view> sizeLimitForOption(Option option) {
	constexpr std::size_t
		MAX_SIZE_DEFAULT  = 2ULL * 1024 * 1024 * 1024,
		MAX_SIZE_REDDIT   = 20ULL * 1024 * 1024,
		MAX_SIZE_MASTODON = 16ULL * 1024 * 1024;

	switch (option) {
		case Option::Mastodon: return { MAX_SIZE_MASTODON, "Mastodon" };
		case Option::Reddit:   return { MAX_SIZE_REDDIT,   "Reddit" };
		default:               return { MAX_SIZE_DEFAULT,  "pdvrdt" };
	}
}

void validateInputs(std::size_t combined_size, const std::string& data_filename, Option option) {

	constexpr std::size_t
		FILENAME_MAX_LEN  = 20;

	if (data_filename.size() > FILENAME_MAX_LEN) {
		throw std::runtime_error("Data File Error: For compatibility requirements, length of data filename must not exceed 20 characters.");
	}

	const auto [limit, label] = sizeLimitForOption(option);

	if (combined_size > limit) {
		throw std::runtime_error(std::format(
			"File Size Error: Combined size of image and data file exceeds maximum size limit for {}.", label));
	}
}

void validateOutputSize(std::size_t output_size, Option option) {
	const auto [limit, label] = sizeLimitForOption(option);
	if (output_size > limit) {
		throw std::runtime_error(std::format(
			"File Size Error: Final output PNG exceeds maximum size limit for {}.", label));
	}
}

[[nodiscard]] std::uint32_t checkedChunkDataSize(std::size_t payload_size, std::size_t chunk_diff) {
	if (payload_size > std::numeric_limits<std::uint32_t>::max() - chunk_diff) {
		throw std::runtime_error("PNG Error: Chunk payload exceeds PNG chunk size limit.");
	}
	return static_cast<std::uint32_t>(payload_size + chunk_diff);
}

[[nodiscard]] std::size_t checkedChunkTotalSize(std::size_t payload_size, std::size_t chunk_diff) {
	constexpr std::size_t CHUNK_OVERHEAD = 12;
	return static_cast<std::size_t>(checkedChunkDataSize(payload_size, chunk_diff)) + CHUNK_OVERHEAD;
}

[[nodiscard]] std::uint32_t checkedChunkDataSizeFromParts(std::initializer_list<std::span<const Byte>> chunk_data_parts) {
	std::size_t total = 0;
	for (const auto part : chunk_data_parts) {
		if (part.size() > std::numeric_limits<std::uint32_t>::max() - total) {
			throw std::runtime_error("PNG Error: Chunk payload exceeds PNG chunk size limit.");
		}
		total += part.size();
	}
	return static_cast<std::uint32_t>(total);
}

[[nodiscard]] std::size_t checkedAddSize(std::size_t lhs, std::size_t rhs, std::string_view message) {
	if (lhs > std::numeric_limits<std::size_t>::max() - rhs) {
		throw std::runtime_error(std::string(message));
	}
	return lhs + rhs;
}

struct OutputFileHandle {
	fs::path path{};
	int fd{-1};
};

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

	uLong crc = crc32(0L, Z_NULL, 0);
	crc = crc32(crc, chunk_type.data(), static_cast<uInt>(chunk_type.size()));

	for (const auto part : chunk_data_parts) {
		writeAllToFd(fd, part);
		if (!part.empty()) {
			crc = crc32(crc, part.data(), static_cast<uInt>(part.size()));
		}
	}

	writeUint32OrThrow(fd, static_cast<std::uint32_t>(crc));
}

void writeRedditPaddingChunk(int fd) {
	constexpr std::uint32_t REDDIT_PADDING_BYTES = 0x80000;
	constexpr auto TYPE_IDAT = std::to_array<Byte>({ 0x49, 0x44, 0x41, 0x54 });
	constexpr auto IDAT_REDDIT_CRC = std::to_array<Byte>({ 0xA3, 0x1A, 0x50, 0xFA });

	writeUint32OrThrow(fd, REDDIT_PADDING_BYTES);
	writeAllToFd(fd, TYPE_IDAT);

	std::array<Byte, 8192> zeros{};
	std::size_t remaining = REDDIT_PADDING_BYTES;
	while (remaining != 0) {
		const std::size_t chunk_size = std::min(remaining, zeros.size());
		writeAllToFd(fd, std::span<const Byte>(zeros.data(), chunk_size));
		remaining -= chunk_size;
	}

	writeAllToFd(fd, IDAT_REDDIT_CRC);
}

[[nodiscard]] std::size_t computeDeflatedSpanSize(std::span<const Byte> data, Option option, bool is_compressed_file) {
	std::size_t compressed_size = 0;
	zlibDeflateSpan(data, option, is_compressed_file, [&](std::span<const Byte> chunk) {
		compressed_size = checkedAddSize(
			compressed_size,
			chunk.size(),
			"File Size Error: Final output size overflow."
		);
	});
	return compressed_size;
}

void writeIccpChunkFromProfile(int fd, std::span<const Byte> profile_data, Option option, bool is_compressed_file, std::size_t expected_compressed_size) {
	constexpr auto TYPE_ICCP = std::to_array<Byte>({ 0x69, 0x43, 0x43, 0x50 });
	constexpr auto ICCP_PREFIX = std::to_array<Byte>({ 0x69, 0x63, 0x63, 0x00, 0x00 });

	writeUint32OrThrow(fd, checkedChunkDataSize(expected_compressed_size, ICCP_PREFIX.size()));
	writeAllToFd(fd, TYPE_ICCP);

	uLong crc = crc32(0L, Z_NULL, 0);
	crc = crc32(crc, TYPE_ICCP.data(), static_cast<uInt>(TYPE_ICCP.size()));
	writeAllToFd(fd, ICCP_PREFIX);
	crc = crc32(crc, ICCP_PREFIX.data(), static_cast<uInt>(ICCP_PREFIX.size()));

	std::size_t actual_compressed_size = 0;
	zlibDeflateSpan(profile_data, option, is_compressed_file, [&](std::span<const Byte> chunk) {
		writeAllToFd(fd, chunk);
		actual_compressed_size = checkedAddSize(
			actual_compressed_size,
			chunk.size(),
			"File Size Error: Final output size overflow."
		);
		if (!chunk.empty()) {
			crc = crc32(crc, chunk.data(), static_cast<uInt>(chunk.size()));
		}
	});

	if (actual_compressed_size != expected_compressed_size) {
		throw std::runtime_error("PNG Error: Streamed iCCP chunk size mismatch.");
	}

	writeUint32OrThrow(fd, static_cast<std::uint32_t>(crc));
}

std::vector<std::string> getCompatiblePlatforms(Option option, std::size_t output_size, bool has_bad_dims, bool twitter_iccp_compatible) {

	if (option == Option::Reddit) {
		return { "Reddit. (Only share this \"file-embedded\" PNG image on Reddit)." };
	}

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

void writeOutputFile(std::size_t output_size, std::uint64_t pin, const std::function<void(int)>& writer) {
	if (!writer) {
		throw std::invalid_argument("Write Error: Output writer is required.");
	}

	OutputFileHandle output_file = createUniqueOutputFile();

	try {
		writer(output_file.fd);
		closeFdOrThrow(output_file.fd);
	} catch (...) {
		closeFdNoThrow(output_file.fd);
		std::error_code ec;
		fs::remove(output_file.path, ec);
		throw;
	}

	std::println("\nSaved \"file-embedded\" PNG image: {} ({} bytes).", output_file.path.string(), output_size);
	std::println("\nRecovery PIN: [***{}***]\n\nImportant: Keep your PIN safe, so that you can extract the hidden file.\n\nComplete!\n", pin);
}
} // namespace

void concealData(vBytes& png_vec, Option option, const fs::path& data_file_path) {
	constexpr std::size_t
		LARGE_FILE_SIZE       = 300ULL * 1024 * 1024,
		MASTODON_INSERT_INDEX = 0x21,
		DEFAULT_INSERT_DIFF   = 12;

	const bool
		is_mastodon = (option == Option::Mastodon),
		is_reddit   = (option == Option::Reddit);

	const std::size_t data_file_size = getFileSizeChecked(data_file_path);
	std::string data_filename = data_file_path.filename().string();

	if (data_file_size > LARGE_FILE_SIZE) {
		std::println("\nPlease wait. Larger files will take longer to complete this process.");
	}

	const auto [has_bad_dims] = optimizeImage(png_vec);

	if (data_file_size > std::numeric_limits<std::size_t>::max() - png_vec.size()) {
		throw std::runtime_error("File Size Error: Combined size overflow.");
	}
	validateInputs(data_file_size + png_vec.size(), data_filename, option);

	const bool is_compressed = hasFileExtension(data_file_path, {
		".zip", ".jar", ".rar", ".7z", ".bz2", ".gz", ".xz", ".tar", ".lz", ".lz4", ".cab", ".rpm", ".deb",
		".mp4", ".mp3", ".exe", ".jpg", ".jpeg", ".jfif", ".png", ".webp", ".bmp", ".gif", ".ogg", ".flac"
	});

	// Prepare the profile template.
	vBytes profile_vec(
		is_mastodon ? MASTODON_PROFILE.begin() : DEFAULT_PROFILE.begin(),
		is_mastodon ? MASTODON_PROFILE.end()   : DEFAULT_PROFILE.end()
	);

	// Stream-compress and encrypt the data file into the profile.
	const std::uint64_t pin = encryptCompressedFileToProfile(
		profile_vec,
		data_file_path,
		data_filename,
		option,
		is_compressed,
		is_mastodon
	);

	const std::size_t mastodon_compressed_profile_size = is_mastodon
		? computeDeflatedSpanSize(std::span<const Byte>(profile_vec.data(), profile_vec.size()), option, is_compressed)
		: 0;

	// Compute the final output layout and stream it directly to disk.
	bool twitter_iccp_compatible = false;
	std::size_t output_size = png_vec.size();

	if (is_mastodon) {
		constexpr std::size_t
			TWITTER_ICCP_MAX_CHUNK_SIZE = 10ULL * 1024,
			TWITTER_IMAGE_MAX_SIZE      = 5ULL * 1024 * 1024,
			ICCP_SIZE_DIFF              = 5;

		const std::uint32_t mastodon_chunk_size = checkedChunkDataSize(mastodon_compressed_profile_size, ICCP_SIZE_DIFF);
		if (MASTODON_INSERT_INDEX > png_vec.size()) {
			throw std::runtime_error("Image File Error: Invalid PNG insertion point for iCCP chunk.");
		}
		output_size = checkedAddSize(
			output_size,
			checkedChunkTotalSize(mastodon_compressed_profile_size, ICCP_SIZE_DIFF),
			"File Size Error: Final output size overflow."
		);

		twitter_iccp_compatible = (output_size <= TWITTER_IMAGE_MAX_SIZE && mastodon_chunk_size <= TWITTER_ICCP_MAX_CHUNK_SIZE);

		validateOutputSize(output_size, option);
		const auto platforms = getCompatiblePlatforms(option, output_size, has_bad_dims, twitter_iccp_compatible);

		std::println("\nPlatform compatibility for output image:-\n");
		for (const auto& platform : platforms) {
			std::println(" ✓ {}", platform);
		}

		writeOutputFile(output_size, pin, [&](int fd) {
			writeAllToFd(fd, std::span<const Byte>(png_vec.data(), MASTODON_INSERT_INDEX));
			writeIccpChunkFromProfile(
				fd,
				std::span<const Byte>(profile_vec.data(), profile_vec.size()),
				option,
				is_compressed,
				mastodon_compressed_profile_size
			);
			writeAllToFd(
				fd,
				std::span<const Byte>(
					png_vec.data() + static_cast<std::ptrdiff_t>(MASTODON_INSERT_INDEX),
					png_vec.size() - MASTODON_INSERT_INDEX
				)
			);
		});
	} else {
		constexpr std::size_t
			IDAT_SIZE_DIFF = 3,
			REDDIT_PADDING_CHUNK_TOTAL_SIZE = 0x80000 + 12;
		constexpr auto TYPE_IDAT = std::to_array<Byte>({ 0x49, 0x44, 0x41, 0x54 });
		constexpr auto IDAT_PREFIX = std::to_array<Byte>({ 0x78, 0x5E, 0x5C });

		if (png_vec.size() < DEFAULT_INSERT_DIFF) {
			throw std::runtime_error("Image File Error: Invalid PNG insertion point for IDAT chunk.");
		}
		const std::size_t insert_index = png_vec.size() - DEFAULT_INSERT_DIFF;
		output_size = checkedAddSize(
			output_size,
			checkedChunkTotalSize(profile_vec.size(), IDAT_SIZE_DIFF),
			"File Size Error: Final output size overflow."
		);
		if (is_reddit) {
			output_size = checkedAddSize(
				output_size,
				REDDIT_PADDING_CHUNK_TOTAL_SIZE,
				"File Size Error: Final output size overflow."
			);
		}

		validateOutputSize(output_size, option);
		const auto platforms = getCompatiblePlatforms(option, output_size, has_bad_dims, twitter_iccp_compatible);

		std::println("\nPlatform compatibility for output image:-\n");
		for (const auto& platform : platforms) {
			std::println(" ✓ {}", platform);
		}

		writeOutputFile(output_size, pin, [&](int fd) {
			writeAllToFd(fd, std::span<const Byte>(png_vec.data(), insert_index));
			if (is_reddit) {
				writeRedditPaddingChunk(fd);
			}
			writeChunkFromParts(fd, TYPE_IDAT, {
				std::span<const Byte>(IDAT_PREFIX.data(), IDAT_PREFIX.size()),
				std::span<const Byte>(profile_vec.data(), profile_vec.size())
			});
			writeAllToFd(
				fd,
				std::span<const Byte>(
					png_vec.data() + static_cast<std::ptrdiff_t>(insert_index),
					png_vec.size() - insert_index
				)
			);
		});
	}
}
