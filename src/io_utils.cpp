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
	MAX_IMAGE_SIZE     = 8ULL * 1024 * 1024,
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
	if (file_size > MAX_IMAGE_SIZE) {
		throw std::runtime_error("Image File Error: Cover image file exceeds maximum size limit.");
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

[[nodiscard]] bool isReservedEmbeddedFilename(std::string_view filename) {
	return filename == "." || filename == ".." ||
		(!filename.empty() && (filename.front() == '.' || filename.front() == '-' ||
		                       filename.back() == ' ' || filename.back() == '.'));
}
} // namespace

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

bool hasSafeEmbeddedFilename(const fs::path& p) {
	return hasValidFilename(p) && !isReservedEmbeddedFilename(p.filename().string());
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
