#include "conceal.h"
#include "encryption.h"
#include "image.h"
#include "io_utils.h"
#include "png_utils.h"
#include "compression.h"
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>

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

void validateInputs(std::size_t combined_size, const std::string& data_filename, Option option) {

	constexpr std::size_t
		MAX_SIZE_DEFAULT  = 2ULL * 1024 * 1024 * 1024,
		MAX_SIZE_REDDIT   = 20ULL * 1024 * 1024,
		MAX_SIZE_MASTODON = 16ULL * 1024 * 1024,
		FILENAME_MAX_LEN  = 20;

	if (data_filename.size() > FILENAME_MAX_LEN) {
		throw std::runtime_error("Data File Error: For compatibility requirements, length of data filename must not exceed 20 characters.");
	}

	const auto [limit, label] = [&]() -> std::pair<std::size_t, std::string_view> {
		switch (option) {
			case Option::Mastodon: return { MAX_SIZE_MASTODON, "Mastodon" };
			case Option::Reddit:   return { MAX_SIZE_REDDIT,   "Reddit" };
			default:               return { MAX_SIZE_DEFAULT,  "pdvrdt" };
		}
	}();

	if (combined_size > limit) {
		throw std::runtime_error(std::format(
			"File Size Error: Combined size of image and data file exceeds maximum size limit for {}.", label));
	}
}

[[nodiscard]] std::uint32_t checkedChunkDataSize(std::size_t payload_size, std::size_t chunk_diff) {
	if (payload_size > std::numeric_limits<std::uint32_t>::max() - chunk_diff) {
		throw std::runtime_error("PNG Error: Chunk payload exceeds PNG chunk size limit.");
	}
	return static_cast<std::uint32_t>(payload_size + chunk_diff);
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

void closeFdNoThrow(int& fd) noexcept {
	if (fd < 0) {
		return;
	}
	while (::close(fd) < 0 && errno == EINTR) {}
	fd = -1;
}

void closeFdOrThrow(int& fd) {
	if (fd < 0) {
		return;
	}
	while (::close(fd) < 0) {
		if (errno == EINTR) {
			continue;
		}
		const std::error_code ec(errno, std::generic_category());
		throw std::runtime_error(std::format("Write Error: Failed to finalize output file: {}", ec.message()));
	}
	fd = -1;
}

void writeAllOrThrow(int fd, std::span<const Byte> data) {
	std::size_t written = 0;
	while (written < data.size()) {
		const std::size_t remaining = data.size() - written;
		const std::size_t chunk_size = std::min<std::size_t>(remaining, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
		const ssize_t rc = ::write(fd, data.data() + static_cast<std::ptrdiff_t>(written), chunk_size);
		if (rc < 0) {
			if (errno == EINTR) {
				continue;
			}
			const std::error_code ec(errno, std::generic_category());
			throw std::runtime_error(std::format("Write Error: Failed to write complete output file: {}", ec.message()));
		}
		if (rc == 0) {
			throw std::runtime_error("Write Error: Failed to write complete output file.");
		}
		written += static_cast<std::size_t>(rc);
	}
}

vBytes buildIccpChunk(vBytes& profile_vec) {
	constexpr std::size_t
		CHUNK_SIZE_INDEX = 0,
		CHUNK_START      = 4,
		PROFILE_INDEX    = 0x0D,
		SIZE_DIFF        = 5,
		CHUNK_OVERHEAD   = 12, // length(4) + type(4) + crc(4)
		CRC_FIELD_SIZE   = 4;

	// iCCP chunk layout: length(4) + "iCCP" + "icc\0" + compression_method(0) + profile + crc(4)
	const std::uint32_t chunk_data_size = checkedChunkDataSize(profile_vec.size(), SIZE_DIFF);
	const std::size_t total_chunk_size = static_cast<std::size_t>(chunk_data_size) + CHUNK_OVERHEAD;
	vBytes iccp(total_chunk_size, 0x00);

	iccp[4] = 0x69; // i
	iccp[5] = 0x43; // C
	iccp[6] = 0x43; // C
	iccp[7] = 0x50; // P
	iccp[8] = 0x69; // i
	iccp[9] = 0x63; // c
	iccp[10] = 0x63; // c
	iccp[11] = 0x00; // null-terminated profile name
	iccp[12] = 0x00; // deflate compression method

	if (!profile_vec.empty()) {
		std::memcpy(
			iccp.data() + static_cast<std::ptrdiff_t>(PROFILE_INDEX),
			profile_vec.data(),
			profile_vec.size()
		);
	}

	updateValue(iccp, CHUNK_SIZE_INDEX, chunk_data_size);

	const std::uint32_t crc = lodepng_crc32(&iccp[CHUNK_START], chunk_data_size + CRC_FIELD_SIZE);
	updateValue(iccp, (CHUNK_START + chunk_data_size + CRC_FIELD_SIZE), crc);

	return iccp;
}

vBytes buildIdatChunk(vBytes& profile_vec) {
	constexpr std::size_t
		CHUNK_SIZE_INDEX = 0,
		CHUNK_START      = 4,
		PROFILE_INDEX    = 0x0B,
		SIZE_DIFF        = 3,
		CHUNK_OVERHEAD   = 12, // length(4) + type(4) + crc(4)
		CRC_FIELD_SIZE   = 4;

	// IDAT chunk layout: length(4) + "IDAT" + zlib_header(78 5E 5C) + profile + crc(4)
	const std::uint32_t chunk_data_size = checkedChunkDataSize(profile_vec.size(), SIZE_DIFF);
	const std::size_t total_chunk_size = static_cast<std::size_t>(chunk_data_size) + CHUNK_OVERHEAD;
	vBytes idat(total_chunk_size, 0x00);

	idat[4] = 0x49; // I
	idat[5] = 0x44; // D
	idat[6] = 0x41; // A
	idat[7] = 0x54; // T
	idat[8] = 0x78;
	idat[9] = 0x5E;
	idat[10] = 0x5C;

	if (!profile_vec.empty()) {
		std::memcpy(
			idat.data() + static_cast<std::ptrdiff_t>(PROFILE_INDEX),
			profile_vec.data(),
			profile_vec.size()
		);
	}

	updateValue(idat, CHUNK_SIZE_INDEX, chunk_data_size);

	const std::uint32_t crc = lodepng_crc32(&idat[CHUNK_START], chunk_data_size + CRC_FIELD_SIZE);
	updateValue(idat, (chunk_data_size + CHUNK_START + CRC_FIELD_SIZE), crc);

	return idat;
}

void insertRedditPadding(vBytes& png_vec) {
	constexpr std::size_t INSERT_INDEX_DIFF = 12;
	constexpr auto IDAT_REDDIT_CRC = std::to_array<Byte>({ 0xA3, 0x1A, 0x50, 0xFA });

	// 524288-byte empty IDAT chunk for Reddit compatibility.
	vBytes reddit_chunk = { 0x00, 0x08, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54 };
	reddit_chunk.resize(reddit_chunk.size() + 0x80000, 0x00);
	reddit_chunk.insert(reddit_chunk.end(), IDAT_REDDIT_CRC.begin(), IDAT_REDDIT_CRC.end());

	if (png_vec.size() < INSERT_INDEX_DIFF) {
		throw std::runtime_error("Image File Error: PNG metadata is too short for Reddit padding.");
	}
	png_vec.reserve(png_vec.size() + reddit_chunk.size());
	png_vec.insert(png_vec.end() - INSERT_INDEX_DIFF, reddit_chunk.begin(), reddit_chunk.end());
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

void writeOutputFile(const vBytes& png_vec, std::size_t pin) {
	OutputFileHandle output_file = createUniqueOutputFile();

	try {
		writeAllOrThrow(output_file.fd, png_vec);
		closeFdOrThrow(output_file.fd);
	} catch (...) {
		closeFdNoThrow(output_file.fd);
		std::error_code ec;
		fs::remove(output_file.path, ec);
		throw;
	}

	std::println("\nSaved \"file-embedded\" PNG image: {} ({} bytes).", output_file.path.string(), png_vec.size());
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

	vBytes data_vec = readFile(data_file_path);

	if (data_vec.size() > LARGE_FILE_SIZE) {
		std::println("\nPlease wait. Larger files will take longer to complete this process.");
	}

	const auto [has_bad_dims] = optimizeImage(png_vec);

	if (data_vec.size() > std::numeric_limits<std::size_t>::max() - png_vec.size()) {
		throw std::runtime_error("File Size Error: Combined size overflow.");
	}
	validateInputs(data_vec.size() + png_vec.size(), data_file_path.filename().string(), option);

	std::string data_filename = data_file_path.filename().string();

	const bool is_compressed = hasFileExtension(data_file_path, {
		".zip", ".jar", ".rar", ".7z", ".bz2", ".gz", ".xz", ".tar", ".lz", ".lz4", ".cab", ".rpm", ".deb",
		".mp4", ".mp3", ".exe", ".jpg", ".jpeg", ".jfif", ".png", ".webp", ".bmp", ".gif", ".ogg", ".flac"
	});

	// Prepare the profile template.
	vBytes profile_vec(
		is_mastodon ? MASTODON_PROFILE.begin() : DEFAULT_PROFILE.begin(),
		is_mastodon ? MASTODON_PROFILE.end()   : DEFAULT_PROFILE.end()
	);

	// Compress the data file.
	zlibDeflate(data_vec, option, is_compressed);

	if (data_vec.empty()) {
		throw std::runtime_error("File Size Error: File is zero bytes. Probable compression failure.");
	}

	// Encrypt and embed data into the profile.
	const std::size_t pin = encryptDataFile(profile_vec, data_vec, data_filename, is_mastodon);

	// Insert Reddit padding IDAT if needed.
	if (is_reddit) {
		insertRedditPadding(png_vec);
	}

	// Compress profile for Mastodon (iCCP requires deflated ICC data).
	if (is_mastodon) {
		zlibDeflate(profile_vec, option, is_compressed);
	}

	// Build and insert the appropriate PNG chunk.
	bool twitter_iccp_compatible = false;

	if (is_mastodon) {
		constexpr std::size_t
			TWITTER_ICCP_MAX_CHUNK_SIZE = 10ULL * 1024,
			TWITTER_IMAGE_MAX_SIZE      = 5ULL * 1024 * 1024,
			ICCP_SIZE_DIFF              = 5;

		const std::size_t mastodon_chunk_size = profile_vec.size() + ICCP_SIZE_DIFF;

		vBytes iccp_chunk = buildIccpChunk(profile_vec);
		if (MASTODON_INSERT_INDEX > png_vec.size()) {
			throw std::runtime_error("Image File Error: Invalid PNG insertion point for iCCP chunk.");
		}
		png_vec.insert(png_vec.begin() + MASTODON_INSERT_INDEX, iccp_chunk.begin(), iccp_chunk.end());

		twitter_iccp_compatible = (png_vec.size() <= TWITTER_IMAGE_MAX_SIZE && mastodon_chunk_size <= TWITTER_ICCP_MAX_CHUNK_SIZE);
	} else {
		vBytes idat_chunk = buildIdatChunk(profile_vec);
		if (png_vec.size() < DEFAULT_INSERT_DIFF) {
			throw std::runtime_error("Image File Error: Invalid PNG insertion point for IDAT chunk.");
		}
		const std::size_t insert_index = png_vec.size() - DEFAULT_INSERT_DIFF;
		png_vec.insert(png_vec.begin() + insert_index, idat_chunk.begin(), idat_chunk.end());
	}

	// Determine platform compatibility and write output.
	const auto platforms = getCompatiblePlatforms(option, png_vec.size(), has_bad_dims, twitter_iccp_compatible);

	std::println("\nPlatform compatibility for output image:-\n");
	for (const auto& platform : platforms) {
		std::println(" ✓ {}", platform);
	}
	writeOutputFile(png_vec, pin);
}
