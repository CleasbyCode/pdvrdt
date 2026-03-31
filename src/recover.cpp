#include "recover.h"
#include "encryption.h"
#include "lodepng/lodepng_config.h"
#include "lodepng/lodepng.h"
#include "png_utils.h"
#include "compression.h"
#include "io_utils.h"

#include <fcntl.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fs.h>
#include <sys/syscall.h>
#endif

#include <cerrno>
#include <cstring>
#include <format>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace {

constexpr auto KDF_MAGIC = std::to_array<Byte>({'K', 'D', 'F', '2'});
constexpr auto PDV_SIG = std::to_array<Byte>({ 0xC6, 0x50, 0x3C, 0xEA, 0x5E, 0x9D, 0xF9 });
constexpr std::size_t DEFAULT_PDV_SIG_OFFSET = 101;
constexpr std::size_t MASTODON_PDV_SIG_OFFSET = 502;

[[nodiscard]] fs::path safeRecoveryPath(std::string decrypted_filename) {
	if (decrypted_filename.empty()) {
		throw std::runtime_error("File Recovery Error: Recovered filename is unsafe.");
	}

	fs::path parsed(std::move(decrypted_filename));
	if (parsed.has_root_path() ||
		parsed.has_parent_path() ||
		parsed != parsed.filename() ||
		!hasValidFilename(parsed)) {
		throw std::runtime_error("File Recovery Error: Recovered filename is unsafe.");
	}

	fs::path candidate = parsed.filename();
	std::error_code ec;
	if (!fs::exists(candidate, ec)) {
		return candidate;
	}

	std::string stem = candidate.stem().string();
	if (stem.empty()) stem = "recovered";
	const std::string ext = candidate.extension().string();

	for (std::size_t i = 1; i <= 10000; ++i) {
		fs::path next(std::format("{}_{}{}", stem, i, ext));
		if (!fs::exists(next, ec)) {
			return next;
		}
	}
	throw std::runtime_error("Write File Error: Unable to create a unique output filename.");
}

struct StagedOutputFile {
	fs::path path{};
	int fd{-1};
};

[[nodiscard]] StagedOutputFile createStagedOutputFile(const fs::path& output_path) {
	constexpr std::size_t MAX_ATTEMPTS = 1024;
	const fs::path parent = output_path.parent_path();
	const std::string base = output_path.filename().string();

	const std::string prefix = std::format(".{}.pdvrdt_tmp_", base);
	for (std::size_t i = 0; i < MAX_ATTEMPTS; ++i) {
		const uint32_t rand_num = 100000 + randombytes_uniform(900000);
		const fs::path candidate = parent / std::format("{}{}", prefix, rand_num);

		int flags = O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC;
#ifdef O_NOFOLLOW
		flags |= O_NOFOLLOW;
#endif

		const int fd = ::open(candidate.c_str(), flags, S_IRUSR | S_IWUSR);
		if (fd >= 0) {
			return StagedOutputFile{ .path = candidate, .fd = fd };
		}

		if (errno == EEXIST) {
			continue;
		}

		const std::error_code ec(errno, std::generic_category());
		throw std::runtime_error(std::format("Write File Error: Unable to create temp output file: {}", ec.message()));
	}
	throw std::runtime_error("Write File Error: Unable to allocate temporary output filename.");
}

void commitRecoveredOutput(const fs::path& staged_path, const fs::path& output_path) {
#ifdef __linux__
	const long rename_rc = ::syscall(
		SYS_renameat2,
		AT_FDCWD, staged_path.c_str(),
		AT_FDCWD, output_path.c_str(),
		RENAME_NOREPLACE
	);
	if (rename_rc == 0) {
		return;
	}
	if (errno != ENOSYS && errno != EINVAL) {
		if (errno == EEXIST) {
			throw std::runtime_error("Write File Error: Output file already exists.");
		}
		const std::error_code ec(errno, std::generic_category());
		throw std::runtime_error(std::format("Write File Error: Failed to commit recovered file: {}", ec.message()));
	}
#endif

#if defined(__unix__) || defined(__APPLE__)
	if (::link(staged_path.c_str(), output_path.c_str()) == 0) {
		cleanupPathNoThrow(staged_path);
		return;
	}
	if (errno == EEXIST) {
		throw std::runtime_error("Write File Error: Output file already exists.");
	}
	{
		const std::error_code ec(errno, std::generic_category());
		throw std::runtime_error(std::format("Write File Error: Failed to commit recovered file: {}", ec.message()));
	}
#endif

	std::error_code ec;
	if (fs::exists(output_path, ec)) {
		throw std::runtime_error("Write File Error: Output file already exists.");
	}
	if (ec) {
		throw std::runtime_error(std::format("Write File Error: Failed to check output path: {}", ec.message()));
	}
	fs::rename(staged_path, output_path, ec);
	if (ec) {
		throw std::runtime_error(std::format("Write File Error: Failed to commit recovered file: {}", ec.message()));
	}
}

struct EmbeddedProfile {
	vBytes profile{};
	bool is_mastodon{false};
};

[[nodiscard]] bool bytesEqualAt(std::span<const Byte> data, std::size_t offset, std::span<const Byte> expected) {
	return spanHasRange(data, offset, expected.size()) &&
		std::memcmp(data.data() + static_cast<std::ptrdiff_t>(offset), expected.data(), expected.size()) == 0;
}

[[nodiscard]] bool hasKdfMetadata(std::span<const Byte> profile, const ProfileOffsets& offsets) {
	constexpr std::size_t MIN_STREAM_CIPHERTEXT_SIZE =
		crypto_secretstream_xchacha20poly1305_HEADERBYTES +
		4 +
		crypto_secretstream_xchacha20poly1305_ABYTES;
	return spanHasRange(profile, offsets.kdf_metadata, KDF_METADATA_REGION_BYTES) &&
		bytesEqualAt(profile, offsets.kdf_metadata + KDF_MAGIC_OFFSET, KDF_MAGIC) &&
		profile[offsets.kdf_metadata + KDF_ALG_OFFSET] == KDF_ALG_ARGON2ID13 &&
		profile[offsets.kdf_metadata + KDF_SENTINEL_OFFSET] == KDF_SENTINEL &&
		spanHasRange(profile, offsets.encrypted_file, MIN_STREAM_CIPHERTEXT_SIZE);
}

[[nodiscard]] std::optional<vBytes> tryExtractDefaultProfileFromIdat(std::span<const Byte> idat_data) {
	constexpr auto IDAT_PREFIX = std::to_array<Byte>({ 0x78, 0x5E, 0x5C });
	if (!bytesEqualAt(idat_data, 0, IDAT_PREFIX)) {
		return std::nullopt;
	}

	const std::span<const Byte> profile = idat_data.subspan(IDAT_PREFIX.size());
	if (profile.empty() ||
		!hasKdfMetadata(profile, DEFAULT_OFFSETS) ||
		!bytesEqualAt(profile, DEFAULT_PDV_SIG_OFFSET, PDV_SIG)) {
		return std::nullopt;
	}
	return vBytes(profile.begin(), profile.end());
}

[[nodiscard]] std::optional<vBytes> tryExtractMastodonProfileFromIccp(std::span<const Byte> iccp_data) {
	constexpr auto ICCP_PREFIX = std::to_array<Byte>({ 0x69, 0x63, 0x63, 0x00, 0x00 }); // "icc\0" + deflate method
	constexpr std::size_t MAX_MASTODON_PROFILE_INFLATE_SIZE = 64ULL * 1024 * 1024;
	if (!bytesEqualAt(iccp_data, 0, ICCP_PREFIX)) {
		return std::nullopt;
	}

	const std::span<const Byte> compressed_profile = iccp_data.subspan(ICCP_PREFIX.size());
	if (compressed_profile.empty()) {
		return std::nullopt;
	}

	vBytes profile;
	try {
		profile = zlibInflateSpanBounded(compressed_profile, MAX_MASTODON_PROFILE_INFLATE_SIZE);
	} catch (const std::runtime_error&) {
		return std::nullopt;
	}
	if (profile.empty() ||
		!hasKdfMetadata(profile, MASTODON_OFFSETS) ||
		!bytesEqualAt(profile, MASTODON_PDV_SIG_OFFSET, PDV_SIG)) {
		return std::nullopt;
	}
	return profile;
}

EmbeddedProfile locateEmbeddedData(const vBytes& png_vec) {
	constexpr auto PNG_SIG = std::to_array<Byte>({ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A });
	constexpr std::uint32_t TYPE_IDAT = 0x49444154u;
	constexpr std::uint32_t TYPE_ICCP = 0x69434350u;
	constexpr std::uint32_t TYPE_IEND = 0x49454E44u;

	requireSpanRange(png_vec, 0, PNG_SIG.size(), "Image File Error: This is not a pdvrdt image.");
	if (std::memcmp(png_vec.data(), PNG_SIG.data(), PNG_SIG.size()) != 0) {
		throw std::runtime_error("Image File Error: This is not a pdvrdt image.");
	}

	std::optional<EmbeddedProfile> embedded_profile{};
	bool has_iend = false;
	std::size_t end_offset = 0;

	auto storeEmbeddedProfile = [&](vBytes profile, bool is_mastodon) {
		if (embedded_profile.has_value()) {
			throw std::runtime_error("Image File Error: Multiple embedded payloads detected.");
		}
		embedded_profile = EmbeddedProfile{
			.profile = std::move(profile),
			.is_mastodon = is_mastodon
		};
	};

	std::size_t pos = PNG_SIG.size();
	while (pos < png_vec.size()) {
		requireSpanRange(png_vec, pos, 8, "Image File Error: Corrupt PNG chunk header.");

		const std::size_t chunk_len = getValue(png_vec, pos, 4);
		const std::size_t type_index = pos + 4;
		const std::size_t data_index = type_index + 4;
		if (chunk_len > png_vec.size() - data_index || 4 > png_vec.size() - (data_index + chunk_len)) {
			throw std::runtime_error("Image File Error: Corrupt PNG chunk length.");
		}

		const std::size_t crc_index = data_index + chunk_len;
		requireSpanRange(png_vec, data_index, chunk_len, "Image File Error: Corrupt PNG chunk length.");
		requireSpanRange(png_vec, crc_index, 4, "Image File Error: Corrupt PNG chunk CRC.");
		const std::uint32_t chunk_type = static_cast<std::uint32_t>(getValue(png_vec, type_index, 4));

		const std::uint32_t stored_crc = static_cast<std::uint32_t>(getValue(png_vec, crc_index, 4));
		const std::uint32_t computed_crc = lodepng_crc32(
			png_vec.data() + static_cast<std::ptrdiff_t>(type_index),
			chunk_len + 4
		);
		if (stored_crc != computed_crc) {
			throw std::runtime_error("Image File Error: Corrupt PNG chunk CRC.");
		}

		const std::span<const Byte> chunk_data(
			png_vec.data() + static_cast<std::ptrdiff_t>(data_index),
			chunk_len
		);

		if (chunk_type == TYPE_ICCP) {
			if (auto profile = tryExtractMastodonProfileFromIccp(chunk_data)) {
				storeEmbeddedProfile(std::move(*profile), true);
			}
		} else if (chunk_type == TYPE_IDAT) {
			if (auto profile = tryExtractDefaultProfileFromIdat(chunk_data)) {
				storeEmbeddedProfile(std::move(*profile), false);
			}
		}

		if (chunk_type == TYPE_IEND) {
			has_iend = true;
			end_offset = crc_index + 4;
			break;
		}

		pos = crc_index + 4;
	}

	if (!has_iend) {
		throw std::runtime_error("Image File Error: Corrupt PNG structure. Missing IEND.");
	}
	if (end_offset != png_vec.size()) {
		throw std::runtime_error("Image File Error: Corrupt PNG structure. Unexpected trailing data after IEND.");
	}
	if (embedded_profile) {
		return std::move(*embedded_profile);
	}
	throw std::runtime_error("Image File Error: This is not a pdvrdt image.");
}

} // namespace

void recoverData(vBytes& png_vec) {
	const auto embedded = locateEmbeddedData(png_vec);
	png_vec = embedded.profile;

	auto result = decryptDataFile(png_vec, embedded.is_mastodon);
	if (!result) {
		throw std::runtime_error("File Recovery Error: Invalid PIN or file is corrupt.");
	}

	fs::path output_path = safeRecoveryPath(std::move(*result));
	StagedOutputFile staged_file = createStagedOutputFile(output_path);
	std::size_t recovered_size = 0;

	try {
		recovered_size = zlibInflateToFd(png_vec, staged_file.fd);
		closeFdOrThrow(staged_file.fd);
		commitRecoveredOutput(staged_file.path, output_path);
	} catch (...) {
		closeFdNoThrow(staged_file.fd);
		cleanupPathNoThrow(staged_file.path);
		throw;
	}

	std::println("\nExtracted hidden file: {} ({} bytes).\n\nComplete! Please check your file.\n",
		output_path.string(), recovered_size);
}
