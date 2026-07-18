#include "recover.h"
#include "encryption.h"
#include "png_utils.h"
#include "compression.h"
#include "io_utils.h"

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>

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
		!hasSafeEmbeddedFilename(parsed)) {
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

// Linux-only atomic publish: renameat2(RENAME_NOREPLACE) avoids the classic
// exists-then-rename TOCTOU. No link()/fs::rename fallbacks — this tool targets
// Linux kernels that provide renameat2.
void commitRecoveredOutput(const fs::path& staged_path, const fs::path& output_path) {
	const long rename_rc = ::syscall(
		SYS_renameat2,
		AT_FDCWD, staged_path.c_str(),
		AT_FDCWD, output_path.c_str(),
		RENAME_NOREPLACE
	);
	if (rename_rc == 0) {
		return;
	}
	if (errno == EEXIST) {
		throw std::runtime_error("Write File Error: Output file already exists.");
	}
	const std::error_code ec(errno, std::generic_category());
	throw std::runtime_error(std::format(
		"Write File Error: Failed to commit recovered file: {}", ec.message()));
}

struct EmbeddedProfile {
	bool is_mastodon{false};
	// Default mode: profile lives within png_vec at [offset, offset+length). No allocation.
	std::size_t offset{0};
	std::size_t length{0};
	// Mastodon mode: profile is the inflate output (allocated).
	vBytes decompressed{};
};

template <typename T>
struct Wiper {
	T& target;

	~Wiper() {
		if (!target.empty()) {
			sodium_memzero(target.data(), target.size());
		}
	}
};

[[nodiscard]] bool bytesEqualAt(std::span<const Byte> data, std::size_t offset, std::span<const Byte> expected) {
	return spanHasRange(data, offset, expected.size()) &&
		std::memcmp(data.data() + static_cast<std::ptrdiff_t>(offset), expected.data(), expected.size()) == 0;
}

[[nodiscard]] bool hasRecoverableKdfMetadata(std::span<const Byte> profile, const ProfileOffsets& offsets) {
	constexpr std::size_t MIN_STREAM_CIPHERTEXT_SIZE =
		crypto_secretstream_xchacha20poly1305_HEADERBYTES +
		4 +
		crypto_secretstream_xchacha20poly1305_ABYTES;
	return hasSupportedKdfMetadataAt(profile, offsets.kdf_metadata) &&
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
	if (!hasRecoverableKdfMetadata(profile, DEFAULT_OFFSETS) ||
		!bytesEqualAt(profile, DEFAULT_PDV_SIG_OFFSET, PDVRDT_SIG)) {
		return std::nullopt;
	}
	return std::make_pair(chunk_offset + CHUNK_HEADER_BYTES + IDAT_PREFIX.size(), profile.size());
}

[[nodiscard]] std::optional<vBytes> tryExtractMastodonProfileFromIccp(std::span<const Byte> iccp_data) {
	constexpr auto ICCP_PREFIX = std::to_array<Byte>({ 0x69, 0x63, 0x63, 0x00, 0x00 }); // "icc\0" + deflate method
	constexpr std::size_t PROFILE_PREFIX_SIZE = MASTODON_PDV_SIG_OFFSET + PDVRDT_SIG.size();
	if (!bytesEqualAt(iccp_data, 0, ICCP_PREFIX)) {
		return std::nullopt;
	}

	const std::span<const Byte> compressed_profile = iccp_data.subspan(ICCP_PREFIX.size());
	if (compressed_profile.empty()) {
		return std::nullopt;
	}

	// Reject ordinary ICC profiles and simple decompression bombs after inflating
	// only the fixed metadata/signature prefix. A matching candidate is then
	// inflated fully under the shared 64 MiB recovery ceiling.
	vBytes profile_prefix;
	try {
		profile_prefix = zlibInflatePrefix(compressed_profile, PROFILE_PREFIX_SIZE);
	} catch (const std::runtime_error&) {
		return std::nullopt;
	}
	if (profile_prefix.size() != PROFILE_PREFIX_SIZE ||
		!hasSupportedKdfMetadataAt(profile_prefix, MASTODON_OFFSETS.kdf_metadata) ||
		!bytesEqualAt(profile_prefix, MASTODON_PDV_SIG_OFFSET, PDVRDT_SIG)) {
		return std::nullopt;
	}

	vBytes profile;
	try {
		profile = zlibInflateSpanBounded(compressed_profile, MAX_MASTODON_PROFILE_BYTES);
	} catch (const std::runtime_error&) {
		return std::nullopt;
	}
	if (!hasRecoverableKdfMetadata(profile, MASTODON_OFFSETS) ||
		!bytesEqualAt(profile, MASTODON_PDV_SIG_OFFSET, PDVRDT_SIG)) {
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
	bool has_iccp = false;
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
			if (has_iccp) {
				throw std::runtime_error("Image File Error: Corrupt PNG structure. Duplicate iCCP chunk.");
			}
			has_iccp = true;
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

void replaceWithEmbeddedProfile(vBytes& png_vec, EmbeddedProfile&& embedded) {
	if (embedded.is_mastodon) {
		png_vec = std::move(embedded.decompressed);
		return;
	}

	std::memmove(png_vec.data(), png_vec.data() + embedded.offset, embedded.length);
	png_vec.resize(embedded.length);
}

[[nodiscard]] std::size_t writeRecoveredPayload(const vBytes& compressed_payload, const fs::path& output_path) {
	StagedOutputFile staged_file = createStagedOutputFile(output_path);
	std::size_t recovered_size = 0;

	try {
		recovered_size = zlibInflateToFd(compressed_payload, staged_file.fd);
		closeFdOrThrow(staged_file.fd);
		commitRecoveredOutput(staged_file.path, output_path);
	} catch (...) {
		closeFdNoThrow(staged_file.fd);
		cleanupPathNoThrow(staged_file.path);
		throw;
	}

	return recovered_size;
}

} // namespace

void recoverData(vBytes& png_vec) {
	auto embedded = locateEmbeddedData(png_vec);
	const bool is_mastodon = embedded.is_mastodon;
	replaceWithEmbeddedProfile(png_vec, std::move(embedded));
	// Decryption is in-place. Install the guard before authentication/parsing so
	// every exception after plaintext first exists still scrubs the allocation.
	Wiper<vBytes> plaintext_wiper{png_vec};

	auto result = decryptDataFile(png_vec, is_mastodon);
	if (!result) {
		throw std::runtime_error("File Recovery Error: Invalid PIN or file is corrupt.");
	}

	// The std::move into safeRecoveryPath transfers ownership to fs::path internal
	// storage (which we can't portably wipe), but this still scrubs *result on
	// any exception path between here and the move.
	Wiper<std::string> filename_wiper{*result};

	fs::path output_path = safeRecoveryPath(std::move(*result));
	const std::size_t recovered_size = writeRecoveredPayload(png_vec, output_path);

	std::println("\nExtracted hidden file: {} ({} bytes).\n\nComplete! Please check your file.\n",
		output_path.string(), recovered_size);
}
