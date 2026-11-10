#include "recover.h"
#include "encryption.h"
#include "png_utils.h"
#include "reddit_steg.h"
#include "compression.h"
#include "io_utils.h"
#include "step_output.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <functional>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
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
// on a UTF-8 sequence boundary; a pathologically long extension is dropped,
// including when it leaves no room for the fallback stem.
[[nodiscard]] fs::path numberedRecoveryName(const fs::path& base, std::size_t index) {
	constexpr std::string_view FALLBACK_STEM = "recovered";
	std::string stem = base.stem().string();
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
	}
	if (stem.empty()) {
		// The suffix is at most "_10000", so without the extension the
		// fallback always fits.
		if (FALLBACK_STEM.size() > max_stem) ext.clear();
		stem = FALLBACK_STEM;
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

struct EmbeddedProfile {
	bool is_mastodon{false};
	// Default mode: profile lives within png_vec at [offset, offset+length). No allocation.
	std::size_t offset{0};
	std::size_t length{0};
	// Mastodon mode: profile is the inflate output (allocated).
	vBytes decompressed{};
	// Data length of the chunk that carries it, for the step output.
	std::size_t chunk_size{0};
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
	const PngChunkView ihdr = readRequiredIhdr(png_vec);

	std::optional<EmbeddedProfile> embedded_profile{};
	bool has_iccp = false;
	std::size_t end_offset = 0;

	const auto storeProfile = [&](EmbeddedProfile&& profile) {
		if (embedded_profile.has_value()) {
			throw std::runtime_error("Image File Error: Multiple embedded payloads detected.");
		}
		embedded_profile = std::move(profile);
	};

	forEachChunkToIend(png_vec, ihdr.offset + ihdr.total_size, [&](const PngChunkView& chunk) {
		switch (chunk.type) {
			case TYPE_IHDR:
				throw std::runtime_error("Image File Error: Corrupt PNG structure. Duplicate IHDR.");
			case TYPE_ICCP:
				if (has_iccp) {
					throw std::runtime_error("Image File Error: Corrupt PNG structure. Duplicate iCCP chunk.");
				}
				has_iccp = true;
				if (auto profile = tryExtractMastodonProfileFromIccp(chunk.data)) {
					storeProfile(EmbeddedProfile{
						.is_mastodon = true, .decompressed = std::move(*profile), .chunk_size = chunk.data.size() });
				}
				break;
			case TYPE_IDAT:
				if (auto loc = tryLocateDefaultProfileInIdat(chunk.offset, chunk.data)) {
					storeProfile(EmbeddedProfile{
						.is_mastodon = false, .offset = loc->first, .length = loc->second, .chunk_size = chunk.data.size() });
				}
				break;
			case TYPE_IEND:
				end_offset = chunk.offset + chunk.total_size;
				break;
			default:
				break;
		}
	});

	// Drop anything past IEND rather than refusing the image, matching conceal:
	// compactChunksAfterIhdr() truncates a cover there without comment. Those
	// bytes are not part of the image and not part of the payload -- the chunk
	// carrying it sits before IEND and its CRC has already been checked -- so
	// failing on them only made an otherwise intact file unrecoverable. The
	// Reddit carrier below reads pixels, which the same reasoning covers.
	png_vec.resize(end_offset);
	return embedded_profile;
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

// Give the finished, flushed output its name: `base_name` or the first free
// numbered variant. Losing the race for a name is retried with the next free
// one rather than thrown away after a full decrypt and inflate.
[[nodiscard]] RecoveredOutput publishRecoveredOutput(
	OutputFile& output_file, const fs::path& base_name, std::size_t recovered_size) {
	constexpr std::size_t MAX_COMMIT_ATTEMPTS = 16;
	// Flush the payload before publishing the name, so a crash cannot leave the
	// final filename pointing at a truncated or empty file.
	fsyncFdOrThrow(output_file.fd());

	for (std::size_t attempt = 0; attempt < MAX_COMMIT_ATTEMPTS; ++attempt) {
		fs::path output_path = findAvailableRecoveryPath(base_name);
		if (!output_file.publish(output_path)) {
			continue;
		}
		try {
			output_file.close();
		} catch (...) {
			cleanupPathNoThrow(output_path);
			throw;
		}
		fsyncParentDirectoryNoThrow(output_path);
		return RecoveredOutput{ .path = std::move(output_path), .size = recovered_size };
	}
	throw std::runtime_error("Write File Error: Unable to create a unique output filename.");
}

// ------------------------------- step output -------------------------------
//
// Recover reports its steps as conceal does: Locating hidden data, the KEY,
// Deriving key, Decrypting, Decompressing, Writing output file.

constexpr std::string_view KEY_DERIVATION_RESULT = "Argon2id from the 128-bit recovery KEY.";
constexpr std::string_view DECRYPTION_RESULT = "XChaCha20-Poly1305, authenticated.";
constexpr std::string_view REDDIT_CARRIER_RESULT =
	"Reddit mode, adaptive (1,15,4) matrix embedding in the least significant bits of keyed RGB samples.";
constexpr const char* WRONG_KEY_ERROR = "File Recovery Error: Invalid recovery KEY or file is corrupt.";

// Payloads above this size get the wait note on the Decrypting step.
constexpr std::size_t LARGE_PAYLOAD_SIZE = 300uz * 1024 * 1024;

[[nodiscard]] std::string chunkModeResult(bool is_mastodon, std::size_t chunk_size) {
	return is_mastodon
		? std::format("Mastodon mode, iCCP chunk ({}).", formatSize(chunk_size))
		: std::format("Default mode, IDAT chunk ({}).", formatSize(chunk_size));
}

// Conceal stores an already-compressed file instead of deflating it, and only
// that stored stream starts with this zlib header (FLEVEL 0); deflated payloads
// start 78 9C. A stored payload was never compressed, so it gets no
// Decompressing step, as in jdvrif.
[[nodiscard]] bool isStoredZlibStream(std::span<const Byte> payload) {
	return payload.size() >= 2 && payload[0] == 0x78 && payload[1] == 0x01;
}

[[nodiscard]] std::string decompressionResult(std::size_t compressed_size, std::size_t output_size) {
	return std::format("{} -> {}.", formatSizePrecise(compressed_size), formatSizePrecise(output_size));
}

// The two steps around one decrypt call: Deriving key until the Argon2id run
// ends (on_key_derived), then Decrypting until the caller reports the outcome.
class DecryptSteps {
public:
	explicit DecryptSteps(bool large_payload) : large_payload_(large_payload) {
		beginStep("Deriving key");
	}

	void keyDerived() {
		endStep(KEY_DERIVATION_RESULT);
		beginStep("Decrypting");
		wait_shown_ = showWaitNote(large_payload_);
	}

	void succeeded() { endStepAfterWait(wait_shown_, DECRYPTION_RESULT); }

	[[noreturn]] void failed(std::string_view result, const char* error) {
		clearWaitNote(wait_shown_);
		failStep(result);
		throw std::runtime_error(error);
	}

private:
	bool large_payload_;
	bool wait_shown_{false};
};

// Writing output file: give the flushed output its name, then report it.
void finishRecovery(OutputFile& output_file, const fs::path& base_name, std::size_t recovered_size) {
	beginStep("Writing output file");
	const RecoveredOutput output = publishRecoveredOutput(output_file, base_name, recovered_size);
	// The name came from the image, so escape anything a terminal might act on.
	endStep(std::format("{} ({}).", printableFilename(output.path.string()), formatSizePrecise(output.size)));
	std::println("\nComplete! Please check your file.\n");
}

// Decompressing (unless the payload was stored) and Writing output file for a
// payload decrypted in memory.
void writeRecoveredPayload(const vBytes& compressed_payload, const fs::path& base_name) {
	// Unnamed until published, so an interrupted inflate leaves no partial
	// plaintext behind (see OutputFile).
	OutputFile output_file = OutputFile::createInCurrentDirectory();
	const bool stored = isStoredZlibStream(compressed_payload);
	if (!stored) beginStep("Decompressing");
	const std::size_t recovered_size = zlibInflateToFd(compressed_payload, output_file.fd());
	if (!stored) endStep(decompressionResult(compressed_payload.size(), recovered_size));
	finishRecovery(output_file, base_name, recovered_size);
}

// ------------------------- streamed default-mode recovery -------------------------
//
// A default-mode image can hold a payload of up to 2 GiB in one IDAT chunk.
// Reading the file whole to find and decrypt it held all of it in memory, so
// a default-mode payload is instead located by walking the chunk headers in
// the file, and then read once, frame by frame: each frame is authenticated,
// decrypted and inflated straight into the unnamed output, which is only
// given its name once the whole stream has authenticated, the inflate has
// ended cleanly and the chunk's CRC has matched. Memory stays at a few MiB.
//
// Anything else -- a Mastodon or Reddit image, no payload, several payloads,
// or a structure the walk below does not accept -- goes the whole-file way, so
// those images, and every error they report, behave exactly as before.

struct StreamedProfile {
	std::size_t chunk_offset{};    // the payload IDAT chunk, in the file
	std::size_t chunk_length{};    // its data length
	std::size_t profile_offset{};  // the profile, after the 3-byte prefix
	std::size_t profile_length{};
};

// Larger ancillary chunks than this are left to the whole-file path.
constexpr std::size_t MAX_SCANNED_CHUNK_BYTES = 16uz * 1024 * 1024;
constexpr std::size_t STREAM_READ_BYTES = 4uz * 1024 * 1024;

[[nodiscard]] std::uint32_t crcOfFileRange(int fd, std::uint32_t crc, std::size_t offset, std::size_t length) {
	vBytes block(std::min(length, STREAM_READ_BYTES));
	while (length > 0) {
		const std::size_t take = std::min(length, block.size());
		preadExact(fd, std::span(block).first(take), offset);
		crc = pdvrdtCrc32Update(crc, std::span<const Byte>(block).first(take));
		offset += take;
		length -= take;
	}
	return crc;
}

// The single default-mode payload of a cleanly structured PNG, or nullopt to
// leave the image to the whole-file path. Every chunk's CRC is checked here
// except the payload's own, which is checked as it is streamed.
[[nodiscard]] std::optional<StreamedProfile> scanForStreamedProfile(const OpenInputFile& file) {
	const int fd = file.fd();
	const std::size_t size = file.size();
	try {
		std::array<Byte, PNG_HEADER_SIZE + PNG_CHUNK_OVERHEAD + IHDR_DATA_SIZE> head{};
		if (size < head.size()) {
			return std::nullopt;
		}
		preadExact(fd, head, 0);
		const PngChunkView ihdr = readRequiredIhdr(head);

		std::optional<StreamedProfile> found;
		bool has_iccp = false;
		std::size_t pos = ihdr.offset + ihdr.total_size;
		vBytes profile_head(PDVRDT_IDAT_PREFIX.size() + DEFAULT_OFFSETS.encrypted_file);
		while (true) {
			if (size - pos < PNG_CHUNK_OVERHEAD) {
				return std::nullopt;
			}
			std::array<Byte, 8> header{};
			preadExact(fd, header, pos);
			const std::size_t length = getValue(header, 0);
			const std::uint32_t type = getValue(header, 4);
			if (length > 0x7FFFFFFFuz || length > size - pos - PNG_CHUNK_OVERHEAD) {
				return std::nullopt;
			}
			std::array<Byte, 4> stored_crc{};
			preadExact(fd, stored_crc, pos + 8 + length);

			if (type == TYPE_IDAT &&
				length >= profile_head.size() + minimumStreamCipherSize()) {
				preadExact(fd, profile_head, pos + 8);
				const std::span<const Byte> profile = std::span<const Byte>(profile_head).subspan(PDVRDT_IDAT_PREFIX.size());
				if (bytesEqualAt(profile_head, 0, PDVRDT_IDAT_PREFIX) &&
					hasPdvrdtProfileMarkers(profile, DEFAULT_OFFSETS)) {
					if (found) {
						return std::nullopt;  // several payloads: reported by the whole-file path
					}
					found = StreamedProfile{
						.chunk_offset = pos,
						.chunk_length = length,
						.profile_offset = pos + 8 + PDVRDT_IDAT_PREFIX.size(),
						.profile_length = length - PDVRDT_IDAT_PREFIX.size(),
					};
					pos += PNG_CHUNK_OVERHEAD + length;
					continue;
				}
			}

			std::uint32_t crc = pdvrdtCrc32Update(0, std::span<const Byte>(header).subspan(4));
			if (type == TYPE_IHDR) {
				return std::nullopt;
			}
			if (type == TYPE_ICCP) {
				if (has_iccp || length > MAX_SCANNED_CHUNK_BYTES) {
					return std::nullopt;
				}
				has_iccp = true;
				vBytes data(length);
				preadExact(fd, data, pos + 8);
				if (findPdvrdtIccpPayload(data)) {
					return std::nullopt;  // a Mastodon payload
				}
				crc = pdvrdtCrc32Update(crc, data);
			} else {
				crc = crcOfFileRange(fd, crc, pos + 8, length);
			}
			if (crc != getValue(stored_crc, 0)) {
				return std::nullopt;
			}
			if (type == TYPE_IEND) {
				return length == 0 ? found : std::nullopt;
			}
			pos += PNG_CHUNK_OVERHEAD + length;
		}
	} catch (const std::exception&) {
		return std::nullopt;
	}
}

// Sequential reads of the payload's ciphertext, a few MiB at a time, folding
// every byte into the chunk CRC.
class CiphertextReader {
public:
	CiphertextReader(int fd, std::size_t offset, std::size_t length, std::uint32_t crc)
		: fd_(fd), next_(offset), left_in_file_(length), buffer_(std::min(length, STREAM_READ_BYTES)), crc_(crc) {}

	void read(std::span<Byte> out) {
		while (!out.empty()) {
			if (at_ == filled_) {
				refill();
			}
			const std::size_t take = std::min(out.size(), filled_ - at_);
			std::memcpy(out.data(), buffer_.data() + at_, take);
			at_ += take;
			out = out.subspan(take);
		}
	}

	[[nodiscard]] std::uint32_t crc() const noexcept { return crc_; }

private:
	void refill() {
		if (left_in_file_ == 0) {
			throw std::runtime_error("File Recovery Error: Embedded profile is corrupt.");
		}
		const std::size_t take = std::min(left_in_file_, buffer_.size());
		preadExact(fd_, std::span(buffer_).first(take), next_);
		crc_ = pdvrdtCrc32Update(crc_, std::span<const Byte>(buffer_).first(take));
		next_ += take;
		left_in_file_ -= take;
		filled_ = take;
		at_ = 0;
	}

	int fd_;
	std::size_t next_;
	std::size_t left_in_file_;
	vBytes buffer_;
	std::size_t filled_{0};
	std::size_t at_{0};
	std::uint32_t crc_;
};

// Fills the recovery KEY once the image has been found to take one: terminal
// entry here; the web build's pdv_out parses its KEY file instead.
using KeySource = std::function<void(RecoveryKey&)>;

void recoverStreamedProfile(const OpenInputFile& file, const StreamedProfile& found, const KeySource& take_key) {
	const int fd = file.fd();
	vBytes head(DEFAULT_OFFSETS.encrypted_file);
	preadExact(fd, head, found.profile_offset);
	if (kdfMetadataVersionAt(head, DEFAULT_OFFSETS.kdf_metadata) != KdfVersion::key_v3) {
		throw std::runtime_error(UNSUPPORTED_FORMAT_ERROR);
	}
	RecoveryKey recovery_key;
	take_key(recovery_key);

	// The chunk CRC covers its type and all of its data: the prefix, the
	// profile head and the ciphertext, which the reader adds as it goes.
	static constexpr std::array<Byte, 4> IDAT_TYPE = {'I', 'D', 'A', 'T'};
	std::uint32_t crc = pdvrdtCrc32Update(0, IDAT_TYPE);
	crc = pdvrdtCrc32Update(crc, PDVRDT_IDAT_PREFIX);
	crc = pdvrdtCrc32Update(crc, head);
	const std::size_t ciphertext_length = found.profile_length - head.size();
	CiphertextReader reader(fd, found.profile_offset + head.size(), ciphertext_length, crc);

	// Unnamed until published, so an interrupted recovery leaves no partial
	// plaintext behind (see OutputFile).
	OutputFile output_file = OutputFile::createInCurrentDirectory();
	ZlibInflateToFd inflater(output_file.fd());

	// The plaintext starts with the filename: a length byte, then the name.
	std::string filename;
	ScopedWipe filename_wipe{filename};
	std::size_t filename_length = 0;
	bool have_length = false;
	std::optional<fs::path> base_name;
	// The zlib header's two bytes, to tell a stored payload from a deflated one.
	std::array<Byte, 2> zlib_head{};
	std::size_t compressed_size = 0;
	const auto on_plaintext = [&](std::span<const Byte> plaintext) {
		if (!have_length && !plaintext.empty()) {
			filename_length = plaintext[0];
			if (filename_length == 0) {
				throw std::runtime_error("File Recovery Error: Embedded profile is corrupt.");
			}
			have_length = true;
			plaintext = plaintext.subspan(1);
		}
		if (!base_name) {
			const std::size_t take = std::min(plaintext.size(), filename_length - filename.size());
			filename.append(reinterpret_cast<const char*>(plaintext.data()), take);
			plaintext = plaintext.subspan(take);
			if (filename.size() == filename_length) {
				base_name = validatedRecoveryName(filename);
			}
		}
		if (compressed_size < zlib_head.size()) {
			const std::size_t take = std::min(plaintext.size(), zlib_head.size() - compressed_size);
			std::memcpy(zlib_head.data() + compressed_size, plaintext.data(), take);
		}
		compressed_size += plaintext.size();
		if (!plaintext.empty()) {
			inflater.feed(plaintext);
		}
	};

	// Decryption and inflation run as one streamed pass, so the Decompressing
	// step only reports what that pass did.
	DecryptSteps steps(ciphertext_length > LARGE_PAYLOAD_SIZE);
	if (!decryptStreamWithKey(head, ciphertext_length, recovery_key, [&] { steps.keyDerived(); },
			[&](std::span<Byte> out) { reader.read(out); }, on_plaintext)) {
		steps.failed("authentication failed.", WRONG_KEY_ERROR);
	}
	std::array<Byte, 4> stored_crc{};
	preadExact(fd, stored_crc, found.chunk_offset + 8 + found.chunk_length);
	if (reader.crc() != getValue(stored_crc, 0)) {
		steps.failed("IDAT chunk CRC does not match.", "PNG Error: Corrupt PNG chunk CRC.");
	}
	if (!base_name) {
		steps.failed("embedded profile is corrupt.", "File Recovery Error: Embedded profile is corrupt.");
	}
	const std::size_t recovered_size = inflater.finish();
	steps.succeeded();
	if (!isStoredZlibStream(zlib_head)) {
		beginStep("Decompressing");
		endStep(decompressionResult(compressed_size, recovered_size));
	}
	finishRecovery(output_file, *base_name, recovered_size);
}

void recoverWholeImage(vBytes& png_vec, const KeySource& take_key) {
	bool is_mastodon = false;
	RecoveryKey recovery_key;

	if (auto embedded = locateMetadataEmbeddedData(png_vec)) {
		// Metadata modes carry the payload in a chunk, which is locatable without
		// any secret, so the KEY is only needed to decrypt -- and an image too old
		// for this build is reported before the user is asked for one.
		is_mastodon = embedded->is_mastodon;
		endStep(chunkModeResult(is_mastodon, embedded->chunk_size));
		replaceWithEmbeddedProfile(png_vec, std::move(*embedded));
		const std::size_t kdf_index = (is_mastodon ? MASTODON_OFFSETS : DEFAULT_OFFSETS).kdf_metadata;
		if (kdfMetadataVersionAt(png_vec, kdf_index) != KdfVersion::key_v3) {
			throw std::runtime_error(UNSUPPORTED_FORMAT_ERROR);
		}
		take_key(recovery_key);
	} else {
		// No metadata payload, so the only place left is a Reddit pixel carrier.
		// Decoding needs no secret: an image that cannot be one, or will not
		// decode at all, is rejected before the user is asked for a KEY. Past
		// this point a missing carrier and a wrong KEY share one message.
		std::optional<RedditPngCarrier> carrier = decodeRedditRecoveryCarrier(png_vec);
		if (!carrier) {
			failStep("no hidden data found.");
			throw std::runtime_error("Image File Error: This is not a pdvrdt image.");
		}
		endStep("No IDAT or iCCP payload; the KEY is needed to check the pixels.");
		vBytes{}.swap(png_vec);  // the pixels now live in the carrier
		take_key(recovery_key);

		beginStep("Locating pixel carrier");
		auto reddit_profile = extractRedditPngPayload(*carrier, recovery_key);
		if (!reddit_profile) {
			failStep("no hidden data found for this KEY.");
			throw std::runtime_error(
				"File Recovery Error: Invalid recovery KEY, or this is not a pdvrdt image.");
		}
		endStep(REDDIT_CARRIER_RESULT);
		png_vec = std::move(*reddit_profile);
	}
	// Decryption is in-place. Install the guard before authentication/parsing so
	// every exception after plaintext first exists still scrubs the allocation.
	ScopedWipe plaintext_wiper{png_vec};

	DecryptSteps steps(false);  // Mastodon and Reddit payloads are small
	auto result = decryptDataFileWithKey(png_vec, recovery_key, is_mastodon, [&] { steps.keyDerived(); });
	if (!result) {
		steps.failed("authentication failed.", WRONG_KEY_ERROR);
	}
	steps.succeeded();

	// The std::move into validatedRecoveryName transfers ownership to fs::path
	// internal storage (which we can't portably wipe), but this still scrubs
	// *result on any exception path between here and the move.
	ScopedWipe filename_wiper{*result};

	const fs::path base_name = validatedRecoveryName(std::move(*result));
	writeRecoveredPayload(png_vec, base_name);
}

void recoverFromOpenedImage(OpenInputFile& file, const fs::path& image_path, const KeySource& take_key) {
	std::println("\nImage file: {} ({}).\n", printableFilename(image_path.filename().string()), formatSize(file.size()));
	beginStep("Locating hidden data");
	if (const auto streamed = scanForStreamedProfile(file)) {
		endStep(chunkModeResult(false, streamed->chunk_length));
		recoverStreamedProfile(file, *streamed, take_key);
		return;
	}
	vBytes png_vec = readOpenedFile(file, image_path);
	recoverWholeImage(png_vec, take_key);
}

} // namespace

void recoverDataFromFile(const fs::path& image_path) {
	OpenInputFile file = openInputFile(image_path, FileTypeCheck::embedded_image);
	recoverFromOpenedImage(file, image_path, [](RecoveryKey& recovery_key) {
		getRecoveryKey(recovery_key);
		std::println("");
	});
}
