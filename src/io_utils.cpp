#include "io_utils.h"

#include <fcntl.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <format>
#include <ranges>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <unistd.h>

namespace {
constexpr std::size_t
	MAX_FILE_SIZE      = 3ULL * 1024 * 1024 * 1024;

[[nodiscard]] std::size_t safeFileSize(const struct stat& st) {
	if (st.st_size < 0 ||
		static_cast<std::uintmax_t>(st.st_size) > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
		throw std::runtime_error("Error: File is too large for this build.");
	}
	return static_cast<std::size_t>(st.st_size);
}

void requireValidFilenameArgument(const fs::path& path) {
	if (!hasValidFilename(path)) {
		throw std::runtime_error("Invalid Input Error: Unsupported characters in filename arguments.");
	}
}

void requireNonEmptyFile(std::size_t file_size) {
	if (!file_size) {
		throw std::runtime_error("Error: File is empty.");
	}
}

void requirePngFileExtension(const fs::path& path) {
	if (!hasFileExtension(path, {".png"})) {
		throw std::runtime_error("File Type Error: Invalid image extension. Only expecting \".png\".");
	}
}

void requireCoverImageSize(std::size_t file_size) {
	if (file_size > MAX_COVER_IMAGE_SIZE) {
		throw std::runtime_error(std::format(
			"Image File Error: Cover image file exceeds the maximum size limit of {} MiB (image is {} bytes).",
			MAX_COVER_IMAGE_SIZE / (1024 * 1024), file_size));
	}
}

void requireRedditCoverImageSize(std::size_t file_size) {
	if (file_size > MAX_REDDIT_COVER_IMAGE_SIZE) {
		throw std::runtime_error(std::format(
			"Image File Size Error: Cover image file exceeds the maximum size limit of {} MiB for Reddit mode (image is {} bytes).",
			MAX_REDDIT_COVER_IMAGE_SIZE / (1024 * 1024), file_size));
	}
}

void requireFileWithinProgramLimit(std::size_t file_size) {
	if (file_size > MAX_FILE_SIZE) {
		throw std::runtime_error("Error: File exceeds program size limit.");
	}
}

void requireFileTypeConstraints(const fs::path& path, std::size_t file_size, FileTypeCheck file_type) {
	if (file_type == FileTypeCheck::data_file) {
		return;
	}

	requirePngFileExtension(path);
	if (file_type == FileTypeCheck::cover_image) {
		requireCoverImageSize(file_size);
	} else if (file_type == FileTypeCheck::reddit_cover_image) {
		requireRedditCoverImageSize(file_size);
	}
}

[[nodiscard]] std::runtime_error openError(const fs::path& path, int error_number) {
	const std::error_code ec(error_number, std::generic_category());
	return std::runtime_error(std::format(
		"Error: Unable to open file \"{}\": {}.", path.string(), ec.message()));
}
} // namespace

namespace {
// Blacklist: path separators, the Windows-reserved set, control bytes and DEL.
// Everything else (spaces, '+', '#', non-ASCII bytes >= 0x80, ...) is allowed,
// matching the jdvrif family so the three implementations accept/reject the
// same embedded filenames and stay mutually recoverable.
[[nodiscard]] bool isValidFilenameChar(unsigned char c) {
	switch (c) {
		case '/': case '\\': case ':': case '*': case '?':
		case '"': case '<': case '>': case '|':
			return false;
		default:
			return c >= 0x20 && c != 0x7F;
	}
}

// One well-formed UTF-8 sequence starting at `index`: its code point and byte
// length. Length 0 for an invalid, overlong, surrogate or truncated sequence.
struct Utf8Sequence {
	char32_t code_point{};
	std::size_t length{};
};

[[nodiscard]] Utf8Sequence decodeUtf8At(std::string_view text, std::size_t index) {
	const auto byte_at = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
	const unsigned char lead = byte_at(index);
	if (lead < 0x80) {
		return { lead, 1 };
	}

	std::size_t length = 0;
	char32_t code_point = 0;
	char32_t minimum = 0;
	if ((lead & 0xE0) == 0xC0) {
		length = 2; code_point = lead & 0x1F; minimum = 0x80;
	} else if ((lead & 0xF0) == 0xE0) {
		length = 3; code_point = lead & 0x0F; minimum = 0x800;
	} else if ((lead & 0xF8) == 0xF0) {
		length = 4; code_point = lead & 0x07; minimum = 0x10000;
	} else {
		return {};
	}
	if (length > text.size() - index) {
		return {};
	}
	for (std::size_t i = 1; i < length; ++i) {
		const unsigned char next = byte_at(index + i);
		if ((next & 0xC0) != 0x80) {
			return {};
		}
		code_point = (code_point << 6) | (next & 0x3F);
	}
	if (code_point < minimum || code_point > 0x10FFFF ||
		(code_point >= 0xD800 && code_point <= 0xDFFF)) {
		return {};
	}
	return { code_point, length };
}

[[nodiscard]] bool isUnsafeCodePoint(char32_t cp) {
	return
		(cp >= 0x80 && cp <= 0x9F) ||      // C1 controls (e.g. U+009B CSI)
		cp == 0x061C ||                     // arabic letter mark
		cp == 0x200E || cp == 0x200F ||     // LRM / RLM
		(cp >= 0x202A && cp <= 0x202E) ||   // LRE, RLE, PDF, LRO, RLO
		(cp >= 0x2066 && cp <= 0x2069);     // LRI, RLI, FSI, PDI
}

[[nodiscard]] bool hasUnsafeCodePoint(std::string_view name) {
	for (std::size_t i = 0; i < name.size();) {
		const Utf8Sequence seq = decodeUtf8At(name, i);
		if (seq.length == 0) {
			++i;
			continue;
		}
		if (isUnsafeCodePoint(seq.code_point)) {
			return true;
		}
		i += seq.length;
	}
	return false;
}

} // namespace

std::string neutralizeUnsafeCodePoints(std::string_view name) {
	std::string result;
	result.reserve(name.size());
	for (std::size_t i = 0; i < name.size();) {
		const Utf8Sequence seq = decodeUtf8At(name, i);
		if (seq.length == 0) {
			result.push_back(name[i]);
			++i;
			continue;
		}
		if (isUnsafeCodePoint(seq.code_point)) {
			result.push_back('_');
		} else {
			result.append(name.substr(i, seq.length));
		}
		i += seq.length;
	}
	return result;
}

std::string printableFilename(std::string_view name) {
	std::string result;
	result.reserve(name.size());
	const auto escape = [&](unsigned char byte) {
		result += std::format("\\x{:02X}", static_cast<unsigned>(byte));
	};
	for (std::size_t i = 0; i < name.size();) {
		const Utf8Sequence seq = decodeUtf8At(name, i);
		if (seq.length == 0) {
			escape(static_cast<unsigned char>(name[i]));
			++i;
			continue;
		}
		const bool is_control = seq.code_point < 0x20 || seq.code_point == 0x7F;
		if (is_control || isUnsafeCodePoint(seq.code_point) || seq.code_point == '\\') {
			for (std::size_t j = 0; j < seq.length; ++j) {
				escape(static_cast<unsigned char>(name[i + j]));
			}
		} else {
			result.append(name.substr(i, seq.length));
		}
		i += seq.length;
	}
	return result;
}

std::string_view embeddedFilenameProblem(const fs::path& p) {
	if (p.empty() || p.filename().string().empty()) {
		return "the filename is empty";
	}
	const std::string filename = p.filename().string();
	if (!std::ranges::all_of(filename, isValidFilenameChar)) {
		return "the filename contains a path separator, a control character, "
			"or one of the reserved characters : * ? \" < > |";
	}
	if (hasUnsafeCodePoint(filename)) {
		return "the filename contains a Unicode control or bidirectional-formatting character";
	}
	if (filename == "." || filename == "..") {
		return "\".\" and \"..\" are reserved names";
	}
	if (filename.front() == '.' || filename.front() == '-') {
		return "the filename may not begin with '.' or '-'";
	}
	if (filename.back() == ' ' || filename.back() == '.') {
		return "the filename may not end with a space or a '.'";
	}
	return {};
}

bool hasValidFilename(const fs::path& p) {
	if (p.empty()) {
		return false;
	}
	const std::string filename = p.filename().string();
	if (filename.empty()) {
		return false;
	}
	return std::ranges::all_of(filename, isValidFilenameChar);
}

// Single source of truth: the predicate and the user-facing reason can never
// disagree about which filenames are acceptable.
bool hasSafeEmbeddedFilename(const fs::path& p) {
	return embeddedFilenameProblem(p).empty();
}

bool hasFileExtension(const fs::path& p, std::initializer_list<std::string_view> exts) {
	auto e = p.extension().string();
	std::ranges::transform(e, e.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return std::ranges::any_of(exts, [&e](std::string_view ext) {
		return e == ext;
	});
}

OpenInputFile::OpenInputFile(OpenInputFile&& other) noexcept
	: fd_(std::exchange(other.fd_, -1)), size_(std::exchange(other.size_, 0)) {}

OpenInputFile& OpenInputFile::operator=(OpenInputFile&& other) noexcept {
	if (this != &other) {
		closeFdNoThrow(fd_);
		fd_ = std::exchange(other.fd_, -1);
		size_ = std::exchange(other.size_, 0);
	}
	return *this;
}

OpenInputFile::~OpenInputFile() {
	closeFdNoThrow(fd_);
}

OpenInputFile openInputFile(const fs::path& path, FileTypeCheck file_type) {
	requireValidFilenameArgument(path);
	requireFileTypeConstraints(path, 0, file_type);

	int flags = O_RDONLY | O_CLOEXEC;
#ifdef O_NONBLOCK
	// Opening a path nonblocking prevents a pathname swap to a FIFO from hanging
	// before fstat() can reject it. O_NONBLOCK has no effect on regular files.
	flags |= O_NONBLOCK;
#endif
	const int fd = ::open(path.c_str(), flags);
	if (fd < 0) {
		throw openError(path, errno);
	}

	OpenInputFile file(fd, 0);
	struct stat st{};
	if (::fstat(fd, &st) != 0) {
		throw openError(path, errno);
	}
	if (!S_ISREG(st.st_mode)) {
		throw std::runtime_error(std::format(
			"Error: File \"{}\" not found or not a regular file.", path.string()));
	}

	const std::size_t file_size = safeFileSize(st);
	requireNonEmptyFile(file_size);
	requireFileTypeConstraints(path, file_size, file_type);
	requireFileWithinProgramLimit(file_size);
	file.size_ = file_size;
	return file;
}

vBytes readFile(const fs::path& path, FileTypeCheck file_type) {
	OpenInputFile file = openInputFile(path, file_type);
	const std::size_t file_size = file.size();
	vBytes vec(file_size);
	std::size_t offset = 0;
	while (offset < vec.size()) {
		const std::size_t remaining = vec.size() - offset;
		const std::size_t chunk_size = std::min<std::size_t>(
			remaining, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
		const ssize_t rc = ::read(
			file.fd(), vec.data() + static_cast<std::ptrdiff_t>(offset), chunk_size);
		if (rc < 0) {
			if (errno == EINTR) continue;
			throw openError(path, errno);
		}
		if (rc == 0) {
			throw std::runtime_error("Failed to read full file: partial read");
		}
		offset += static_cast<std::size_t>(rc);
	}
	Byte extra{};
	while (true) {
		const ssize_t rc = ::read(file.fd(), &extra, 1);
		if (rc < 0 && errno == EINTR) continue;
		if (rc < 0) throw openError(path, errno);
		if (rc != 0) {
			throw std::runtime_error("Failed to read file reliably: file grew while being read.");
		}
		break;
	}

	return vec;
}

void closeFdNoThrow(int& fd) noexcept {
	if (fd < 0) return;
	// On Linux, close() always releases the fd even on EINTR.
	// Retrying would risk double-closing a recycled fd.
	::close(fd);
	fd = -1;
}

void closeFdOrThrow(int& fd) {
	if (fd < 0) return;
	// On Linux, close() releases the descriptor even when it reports EINTR.
	// Mark it closed before checking the result: report every finalization error,
	// but never retry and risk closing a recycled descriptor.
	const int saved_fd = fd;
	fd = -1;
	if (::close(saved_fd) < 0) {
		const std::error_code ec(errno, std::generic_category());
		throw std::runtime_error(std::format("Write Error: Failed to finalize output file: {}", ec.message()));
	}
}

void fsyncFdOrThrow(int fd) {
	if (fd < 0) {
		throw std::invalid_argument("fsyncFdOrThrow: valid descriptor is required.");
	}
	while (::fsync(fd) != 0) {
		// POSIX permits fsync() to fail with EINTR without having flushed anything,
		// so this one is genuinely worth retrying (unlike close()).
		if (errno == EINTR) continue;
		const std::error_code ec(errno, std::generic_category());
		throw std::runtime_error(std::format(
			"Write Error: Failed to flush output file to disk: {}", ec.message()));
	}
}

void fsyncParentDirectoryNoThrow(const fs::path& path) noexcept {
	const fs::path parent = path.has_parent_path() ? path.parent_path() : fs::path(".");
	const int dir_fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dir_fd < 0) return;
	while (::fsync(dir_fd) != 0 && errno == EINTR) {
	}
	::close(dir_fd);
}

void writeAllToFd(int fd, std::span<const Byte> data) {
	std::size_t written = 0;
	while (written < data.size()) {
		const std::size_t remaining = data.size() - written;
		const std::size_t chunk_size = std::min<std::size_t>(remaining, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
		const ssize_t rc = ::write(fd, data.data() + static_cast<std::ptrdiff_t>(written), chunk_size);
		if (rc < 0) {
			if (errno == EINTR) continue;
			const std::error_code ec(errno, std::generic_category());
			throw std::runtime_error(std::format("Write Error: Failed to write complete output file: {}", ec.message()));
		}
		if (rc == 0) {
			throw std::runtime_error("Write Error: Failed to write complete output file.");
		}
		written += static_cast<std::size_t>(rc);
	}
}

void cleanupPathNoThrow(const fs::path& path) noexcept {
	if (path.empty()) return;
	std::error_code ec;
	fs::remove(path, ec);
}

void appendBytes(vBytes& out, std::span<const Byte> bytes, std::string_view message) {
	if (bytes.empty()) return;

	const std::size_t base = out.size();
	const std::size_t final_size = checkedAddSize(base, bytes.size(), message);
	out.resize(final_size);
	std::memcpy(out.data() + static_cast<std::ptrdiff_t>(base), bytes.data(), bytes.size());
}

void appendBytesWiping(vBytes& out, std::span<const Byte> bytes, std::string_view message) {
	if (bytes.empty()) return;

	const std::size_t base = out.size();
	const std::size_t final_size = checkedAddSize(base, bytes.size(), message);
	if (final_size > out.capacity()) {
		// Geometric growth, as vector itself would, but through a copy we control
		// so the outgoing buffer can be scrubbed before it is released.
		const std::size_t doubled = out.capacity() > std::numeric_limits<std::size_t>::max() / 2
			? final_size
			: out.capacity() * 2;
		vBytes grown;
		grown.reserve(std::max(final_size, doubled));
		grown.assign(out.begin(), out.end());
		if (!out.empty()) {
			sodium_memzero(out.data(), out.size());
		}
		out.swap(grown);
	}
	out.insert(out.end(), bytes.begin(), bytes.end());
}
