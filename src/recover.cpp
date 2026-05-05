#include "recover.h"
#include "encryption.h"
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

[[nodiscard]] bool pathExistsOrThrow(const fs::path& path, std::string_view context) {
	std::error_code ec;
	const bool exists = fs::exists(path, ec);
	if (ec) {
		throw std::runtime_error(std::format("{}: {}", context, ec.message()));
	}
	return exists;
}

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
	if (!pathExistsOrThrow(candidate, "Write File Error: Failed to check output path")) {
		return candidate;
	}

	std::string stem = candidate.stem().string();
	if (stem.empty()) stem = "recovered";
	const std::string ext = candidate.extension().string();

	for (std::size_t i = 1; i <= 10000; ++i) {
		fs::path next(std::format("{}_{}{}", stem, i, ext));
		if (!pathExistsOrThrow(next, "Write File Error: Failed to check output path")) {
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
	const int link_errno = errno;
	if (link_errno == EEXIST) {
		throw std::runtime_error("Write File Error: Output file already exists.");
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
	bool is_mastodon{false};
	// Default mode: profile lives within png_vec at [offset, offset+length). No allocation.
	std::size_t offset{0};
	std::size_t length{0};
	// Mastodon mode: profile is the inflate output (allocated).
	vBytes decompressed{};
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

// Returns (offset_in_png_vec, length) of the embedded profile, or nullopt if this IDAT
// is not a pdvrdt-encoded one. The 3-byte IDAT_PREFIX sits at chunk.data[0..3]; the
// profile body starts at chunk.offset + 8 (chunk header) + 3 (prefix).
[[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> tryLocateDefaultProfileInIdat(
	std::size_t chunk_offset, std::span<const Byte> idat_data) {
	constexpr auto IDAT_PREFIX = std::to_array<Byte>({ 0x78, 0x5E, 0x5C });
	constexpr std::size_t CHUNK_HEADER_BYTES = 8;
	if (!bytesEqualAt(idat_data, 0, IDAT_PREFIX)) {
		return std::nullopt;
	}

	const std::span<const Byte> profile = idat_data.subspan(IDAT_PREFIX.size());
	if (profile.empty() ||
		!hasKdfMetadata(profile, DEFAULT_OFFSETS) ||
		!bytesEqualAt(profile, DEFAULT_PDV_SIG_OFFSET, PDV_SIG)) {
		return std::nullopt;
	}
	return std::make_pair(chunk_offset + CHUNK_HEADER_BYTES + IDAT_PREFIX.size(), profile.size());
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
	constexpr std::uint32_t TYPE_IHDR = 0x49484452u;
	constexpr std::uint32_t TYPE_IDAT = 0x49444154u;
	constexpr std::uint32_t TYPE_ICCP = 0x69434350u;
	constexpr std::uint32_t TYPE_IEND = 0x49454E44u;
	constexpr std::size_t PNG_HEADER_SIZE = 8;
	constexpr std::size_t IHDR_DATA_SIZE = 13;

	requirePngSignature(png_vec, "Image File Error: This is not a pdvrdt image.");

	std::optional<EmbeddedProfile> embedded_profile{};
	bool has_iend = false;
	bool has_ihdr = false;
	std::size_t end_offset = 0;

	auto storeMastodonProfile = [&](vBytes decompressed) {
		if (embedded_profile.has_value()) {
			throw std::runtime_error("Image File Error: Multiple embedded payloads detected.");
		}
		embedded_profile = EmbeddedProfile{ .is_mastodon = true, .decompressed = std::move(decompressed) };
	};
	auto storeDefaultProfileLocation = [&](std::size_t offset, std::size_t length) {
		if (embedded_profile.has_value()) {
			throw std::runtime_error("Image File Error: Multiple embedded payloads detected.");
		}
		embedded_profile = EmbeddedProfile{ .is_mastodon = false, .offset = offset, .length = length };
	};

	std::size_t pos = PNG_HEADER_SIZE;
	while (pos < png_vec.size()) {
		const PngChunkView chunk = readPngChunk(
			png_vec,
			pos,
			"Image File Error: Corrupt PNG chunk header.",
			"Image File Error: Corrupt PNG chunk length.",
			"Image File Error: Corrupt PNG chunk CRC."
		);

		if (!has_ihdr) {
			if (chunk.type != TYPE_IHDR || chunk.length != IHDR_DATA_SIZE) {
				throw std::runtime_error("Image File Error: Corrupt PNG structure. Missing IHDR.");
			}
			has_ihdr = true;
		} else if (chunk.type == TYPE_IHDR) {
			throw std::runtime_error("Image File Error: Corrupt PNG structure. Duplicate IHDR.");
		}
		if (chunk.type == TYPE_IEND && chunk.length != 0) {
			throw std::runtime_error("Image File Error: Corrupt PNG structure. Invalid IEND.");
		}

		if (chunk.type == TYPE_ICCP) {
			if (auto profile = tryExtractMastodonProfileFromIccp(chunk.data)) {
				storeMastodonProfile(std::move(*profile));
			}
		} else if (chunk.type == TYPE_IDAT) {
			if (auto loc = tryLocateDefaultProfileInIdat(chunk.offset, chunk.data)) {
				storeDefaultProfileLocation(loc->first, loc->second);
			}
		}

		if (chunk.type == TYPE_IEND) {
			has_iend = true;
			end_offset = chunk.offset + chunk.total_size;
			break;
		}

		pos += chunk.total_size;
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
	auto embedded = locateEmbeddedData(png_vec);
	const bool is_mastodon = embedded.is_mastodon;

	if (is_mastodon) {
		png_vec = std::move(embedded.decompressed);
	} else {
		// In-place: shift the profile to the front of png_vec and truncate.
		std::memmove(png_vec.data(), png_vec.data() + embedded.offset, embedded.length);
		png_vec.resize(embedded.length);
	}

	auto result = decryptDataFile(png_vec, is_mastodon);
	if (!result) {
		throw std::runtime_error("File Recovery Error: Invalid PIN or file is corrupt.");
	}

	// After decryption png_vec holds the compressed plaintext of the hidden
	// file. Wipe it on every exit path so it does not persist in freed heap
	// memory after this call returns.
	struct PlaintextWiper {
		vBytes& bytes;
		~PlaintextWiper() {
			if (!bytes.empty()) {
				sodium_memzero(bytes.data(), bytes.size());
			}
		}
	} plaintext_wiper{png_vec};

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
