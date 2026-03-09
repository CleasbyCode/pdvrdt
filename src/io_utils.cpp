#include "io_utils.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <format>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace {
constexpr std::size_t
	MINIMUM_IMAGE_SIZE = 87,
	MAX_IMAGE_SIZE     = 8ULL * 1024 * 1024,
	MAX_FILE_SIZE      = 3ULL * 1024 * 1024 * 1024,
	MAX_STREAM_SIZE    = static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max());

[[nodiscard]] bool isImageType(FileTypeCheck file_type) {
	return file_type == FileTypeCheck::cover_image || file_type == FileTypeCheck::embedded_image;
}

[[nodiscard]] std::size_t safeFileSize(const fs::path& path) {
	std::error_code ec;
	const std::uintmax_t raw_file_size = fs::file_size(path, ec);
	if (ec) {
		throw std::runtime_error(std::format("Error: Failed to get file size for \"{}\".", path.string()));
	}
	if (raw_file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()) ||
		raw_file_size > static_cast<std::uintmax_t>(MAX_STREAM_SIZE)) {
		throw std::runtime_error("Error: File is too large for this build.");
	}
	return static_cast<std::size_t>(raw_file_size);
}

[[nodiscard]] std::size_t validateAndGetFileSize(const fs::path& path, FileTypeCheck file_type) {
	if (!hasValidFilename(path)) {
		throw std::runtime_error("Invalid Input Error: Unsupported characters in filename arguments.");
	}

	std::error_code ec;
	const bool exists = fs::exists(path, ec);
	const bool regular = exists && !ec && fs::is_regular_file(path, ec);
	if (ec || !exists || !regular) {
		throw std::runtime_error(std::format("Error: File \"{}\" not found or not a regular file.", path.string()));
	}

	const std::size_t file_size = safeFileSize(path);

	if (!file_size) {
		throw std::runtime_error("Error: File is empty.");
	}

	if (isImageType(file_type)) {
		if (!hasFileExtension(path, {".png"})) {
			throw std::runtime_error("File Type Error: Invalid image extension. Only expecting \".png\".");
		}

		if (file_type == FileTypeCheck::cover_image) {
			if (MINIMUM_IMAGE_SIZE > file_size) {
				throw std::runtime_error("File Error: Invalid image file size.");
			}

			if (file_size > MAX_IMAGE_SIZE) {
				throw std::runtime_error("Image File Error: Cover image file exceeds maximum size limit.");
			}
		}
	}

	if (file_size > MAX_FILE_SIZE) {
		throw std::runtime_error("Error: File exceeds program size limit.");
	}

	return file_size;
}
}

bool hasValidFilename(const fs::path& p) {
	if (p.empty()) {
		return false;
	}
	std::string filename = p.filename().string();
	if (filename.empty()) {
		return false;
	}
	auto validChar = [](unsigned char c) {
		return std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == '@' || c == '%';
	};
	return std::ranges::all_of(filename, validChar);
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

std::size_t getFileSizeChecked(const fs::path& path, FileTypeCheck file_type) {
	return validateAndGetFileSize(path, file_type);
}

vBytes readFile(const fs::path& path, FileTypeCheck file_type) {
	const std::size_t file_size = getFileSizeChecked(path, file_type);
	std::ifstream file(path, std::ios::binary);

	if (!file) {
		throw std::runtime_error(std::format("Failed to open file: {}", path.string()));
	}

	vBytes vec(file_size);
	const std::streamsize expected = static_cast<std::streamsize>(file_size);
	file.read(reinterpret_cast<char*>(vec.data()), expected);
	if (!file || file.gcount() != expected) {
		throw std::runtime_error("Failed to read full file: partial read");
	}

	return vec;
}

void closeFdNoThrow(int& fd) noexcept {
	if (fd < 0) return;
	while (::close(fd) < 0 && errno == EINTR) {}
	fd = -1;
}

void closeFdOrThrow(int& fd) {
	if (fd < 0) return;
	while (::close(fd) < 0) {
		if (errno == EINTR) continue;
		const std::error_code ec(errno, std::generic_category());
		throw std::runtime_error(std::format("Write Error: Failed to finalize output file: {}", ec.message()));
	}
	fd = -1;
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
