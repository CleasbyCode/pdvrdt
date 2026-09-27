#include "recover.h"
#include "encryption.h"
#include "png_utils.h"
#include "reddit_steg.h"
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

[[nodiscard]] bool pathEntryExistsOrThrow(const fs::path& path, std::string_view context) {
	std::error_code ec;
	// Check the directory entry itself: a dangling or looping symlink still
	// occupies the name and must be skipped before the atomic no-replace commit.
	const fs::file_status status = fs::symlink_status(path, ec);
	if (ec == std::errc::no_such_file_or_directory) {
		return false;
	}
	if (ec) {
		throw std::runtime_error(std::format("{}: {}", context, ec.message()));
	}
	return fs::exists(status);
}

// Linux NAME_MAX: the longest single path component the kernel accepts.
constexpr std::size_t MAX_FILENAME_BYTES = 255;

// The embedded filename, validated as a bare, safe name in the current directory.
// C1 controls and bidi-formatting characters are replaced rather than refused, so
// a payload from an older or foreign release is still recoverable.
[[nodiscard]] fs::path validatedRecoveryName(std::string decrypted_filename) {
	ScopedWipe decrypted_filename_wipe{decrypted_filename};
	if (decrypted_filename.empty()) {
		throw std::runtime_error("File Recovery Error: Recovered filename is unsafe.");
	}

	fs::path parsed(neutralizeUnsafeCodePoints(decrypted_filename));
	if (parsed.has_root_path() ||
		parsed.has_parent_path() ||
		parsed != parsed.filename() ||
		!hasSafeEmbeddedFilename(parsed)) {
		throw std::runtime_error("File Recovery Error: Recovered filename is unsafe.");
	}
	return parsed.filename();
}

// `base` with "_<index>" inserted before its extension, shortened where needed to
// stay within NAME_MAX. The encryption layer allows 255-byte names, so appending
// the suffix untrimmed could produce a name the kernel refuses. The stem is cut
// on a UTF-8 sequence boundary; a pathologically long extension is dropped.
[[nodiscard]] fs::path numberedRecoveryName(const fs::path& base, std::size_t index) {
	std::string stem = base.stem().string();
	if (stem.empty()) stem = "recovered";
	std::string ext = base.extension().string();
	const std::string suffix = std::format("_{}", index);

	if (ext.size() + suffix.size() >= MAX_FILENAME_BYTES) {
		ext.clear();
	}
	const std::size_t max_stem = MAX_FILENAME_BYTES - ext.size() - suffix.size();
	if (stem.size() > max_stem) {
		std::size_t cut = max_stem;
		while (cut > 0 && (static_cast<unsigned char>(stem[cut]) & 0xC0) == 0x80) {
			--cut;
		}
		stem.resize(cut);
		if (stem.empty()) stem = "recovered";
	}
	return fs::path(std::format("{}{}{}", stem, suffix, ext));
}

// First free name for `base`: itself, then base_1, base_2, ...
[[nodiscard]] fs::path findAvailableRecoveryPath(const fs::path& base) {
	constexpr const char* CHECK_ERROR = "Write File Error: Failed to check output path";
	if (!pathEntryExistsOrThrow(base, CHECK_ERROR)) {
		return base;
	}
	for (std::size_t i = 1; i <= 10000; ++i) {
		fs::path next = numberedRecoveryName(base, i);
		if (!pathEntryExistsOrThrow(next, CHECK_ERROR)) {
			return next;
		}
	}
	throw std::runtime_error("Write File Error: Unable to create a unique output filename.");
}

struct StagedOutputFile {
	fs::path path{};
	int fd{-1};
};

// Staged in the current directory -- the only place a recovered file is ever
// written -- under a short fixed-shape name. Deriving the name from the
// recovered filename would push a long (up to 255-byte) name past NAME_MAX.
[[nodiscard]] StagedOutputFile createStagedOutputFile() {
	constexpr std::size_t MAX_ATTEMPTS = 1024;

	for (std::size_t i = 0; i < MAX_ATTEMPTS; ++i) {
		const uint32_t rand_num = 100000 + randombytes_uniform(900000);
		const fs::path candidate = std::format(".pdvrdt_tmp_{}", rand_num);

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
// Linux kernels that provide renameat2. Returns false if `output_path` was
// taken after it was chosen, so the caller can pick the next free name.
[[nodiscard]] bool tryCommitRecoveredOutput(const fs::path& staged_path, const fs::path& output_path) {
	const long rename_rc = ::syscall(
		SYS_renameat2,
		AT_FDCWD, staged_path.c_str(),
		AT_FDCWD, output_path.c_str(),
		RENAME_NOREPLACE
	);
	if (rename_rc == 0) {
		return true;
	}
	if (errno == EEXIST) {
		return false;
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

// The payload fingerprint plus enough ciphertext to actually decrypt. Conceal's
// stripping predicate deliberately omits the length half; see
// hasPdvrdtProfileMarkers().
[[nodiscard]] bool isRecoverableProfile(std::span<const Byte> profile, const ProfileOffsets& offsets) {
	return hasPdvrdtProfileMarkers(profile, offsets) &&
		spanHasRange(profile, offsets.encrypted_file, minimumStreamCipherSize());
}

// Returns (offset_in_png_vec, length) of the embedded profile, or nullopt if this IDAT
// is not a pdvrdt-encoded one. The 3-byte IDAT_PREFIX sits at chunk.data[0..3]; the
// profile body starts at chunk.offset + 8 (chunk header) + 3 (prefix).
[[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> tryLocateDefaultProfileInIdat(
	std::size_t chunk_offset, std::span<const Byte> idat_data) {
	constexpr std::size_t CHUNK_HEADER_BYTES = 8;
	if (!bytesEqualAt(idat_data, 0, PDVRDT_IDAT_PREFIX)) {
		return std::nullopt;
	}

	const std::span<const Byte> profile = idat_data.subspan(PDVRDT_IDAT_PREFIX.size());
	if (!isRecoverableProfile(profile, DEFAULT_OFFSETS)) {
		return std::nullopt;
	}
	return std::make_pair(chunk_offset + CHUNK_HEADER_BYTES + PDVRDT_IDAT_PREFIX.size(), profile.size());
}

[[nodiscard]] std::optional<vBytes> tryExtractMastodonProfileFromIccp(std::span<const Byte> iccp_data) {
	// The cheap prefix-only identification is shared with conceal's stripping path.
	// A matching candidate is then inflated fully under the 64 MiB recovery ceiling.
	const auto compressed_profile = findPdvrdtIccpPayload(iccp_data);
	if (!compressed_profile) {
		return std::nullopt;
	}

	vBytes profile;
	try {
		profile = zlibInflateSpanBounded(*compressed_profile, MAX_MASTODON_PROFILE_BYTES);
	} catch (const std::runtime_error&) {
		return std::nullopt;
	}
	if (!isRecoverableProfile(profile, MASTODON_OFFSETS)) {
		return std::nullopt;
	}
	return profile;
}

std::optional<EmbeddedProfile> locateMetadataEmbeddedData(vBytes& png_vec) {
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
	// Drop anything past IEND rather than refusing the image, matching conceal:
	// compactChunksAfterIhdr() truncates a cover there without comment. Those
	// bytes are not part of the image and not part of the payload -- the chunk
	// carrying it sits before IEND and its CRC has already been checked -- so
	// failing on them only made an otherwise intact file unrecoverable. The
	// Reddit carrier below reads pixels, which the same reasoning covers.
	if (end_offset != png_vec.size()) {
		png_vec.resize(end_offset);
	}
	if (embedded_profile) {
		return embedded_profile;
	}
	return std::nullopt;
}

void replaceWithEmbeddedProfile(vBytes& png_vec, EmbeddedProfile&& embedded) {
	if (embedded.is_mastodon) {
		png_vec = std::move(embedded.decompressed);
		return;
	}

	std::memmove(png_vec.data(), png_vec.data() + embedded.offset, embedded.length);
	png_vec.resize(embedded.length);
}

struct RecoveredOutput {
	fs::path path{};
	std::size_t size{};
};

[[nodiscard]] RecoveredOutput writeRecoveredPayload(const vBytes& compressed_payload, const fs::path& base_name) {
	// Losing the race for a name is retried with the next free one rather than
	// thrown away after a full decrypt and inflate.
	constexpr std::size_t MAX_COMMIT_ATTEMPTS = 16;

	StagedOutputFile staged_file = createStagedOutputFile();

	try {
		const std::size_t recovered_size = zlibInflateToFd(compressed_payload, staged_file.fd);
		// Flush the payload before publishing the name: renameat2() is atomic with
		// respect to the directory entry, but without this a crash can leave the
		// final filename pointing at a truncated or empty file.
		fsyncFdOrThrow(staged_file.fd);
		closeFdOrThrow(staged_file.fd);
		for (std::size_t attempt = 0; attempt < MAX_COMMIT_ATTEMPTS; ++attempt) {
			fs::path output_path = findAvailableRecoveryPath(base_name);
			if (tryCommitRecoveredOutput(staged_file.path, output_path)) {
				fsyncParentDirectoryNoThrow(output_path);
				return RecoveredOutput{ .path = std::move(output_path), .size = recovered_size };
			}
		}
		throw std::runtime_error("Write File Error: Unable to create a unique output filename.");
	} catch (...) {
		closeFdNoThrow(staged_file.fd);
		cleanupPathNoThrow(staged_file.path);
		throw;
	}
}

} // namespace

void recoverData(vBytes& png_vec) {
	bool is_mastodon = false;
	SensitiveU64 recovery_pin;

	if (auto embedded = locateMetadataEmbeddedData(png_vec)) {
		// Metadata modes carry the payload in a chunk, which is locatable without
		// any secret, so the PIN is only needed to decrypt.
		is_mastodon = embedded->is_mastodon;
		replaceWithEmbeddedProfile(png_vec, std::move(*embedded));
		getPin(recovery_pin);
	} else {
		// Carrier recovery derives the version-2 Argon2id key and supports the
		// version-1 key for older images. Missing carriers and wrong PINs share
		// one message; matching headers with structural damage report corruption.
		getPin(recovery_pin);

		auto reddit_profile =
			extractRedditPngPayload(png_vec, recovery_pin);
		if (!reddit_profile) {
			throw std::runtime_error(
				"File Recovery Error: Invalid PIN, or this is not a pdvrdt image.");
		}
		png_vec = std::move(*reddit_profile);
	}
	// Decryption is in-place. Install the guard before authentication/parsing so
	// every exception after plaintext first exists still scrubs the allocation.
	ScopedWipe plaintext_wiper{png_vec};

	auto result = decryptDataFileWithPin(png_vec, recovery_pin, is_mastodon);
	if (!result) {
		throw std::runtime_error("File Recovery Error: Invalid PIN or file is corrupt.");
	}

	// The std::move into validatedRecoveryName transfers ownership to fs::path
	// internal storage (which we can't portably wipe), but this still scrubs
	// *result on any exception path between here and the move.
	ScopedWipe filename_wiper{*result};

	const fs::path base_name = validatedRecoveryName(std::move(*result));
	const RecoveredOutput output = writeRecoveredPayload(png_vec, base_name);

	// The name came from the image, so escape anything a terminal might act on.
	std::println("\nExtracted hidden file: {} ({} bytes).\n\nComplete! Please check your file.\n",
		printableFilename(output.path.string()), output.size);
}
