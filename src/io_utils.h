#pragma once

#include "common.h"

#include <initializer_list>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>

enum class FileTypeCheck : Byte {
	cover_image    = 1,
	embedded_image = 2,
	data_file      = 3
};

[[nodiscard]] bool hasValidFilename(const fs::path& p);
// hasValidFilename plus the reserved-name rules used for embedded/recovered
// filenames: rejects ".", "..", a leading '.' or '-', and a trailing space or '.'.
[[nodiscard]] bool hasSafeEmbeddedFilename(const fs::path& p);
[[nodiscard]] bool hasFileExtension(const fs::path& p, std::initializer_list<std::string_view> exts);
[[nodiscard]] std::size_t getFileSizeChecked(const fs::path& path, FileTypeCheck file_type = FileTypeCheck::data_file);
[[nodiscard]] vBytes readFile(const fs::path& path, FileTypeCheck file_type = FileTypeCheck::data_file);

// Centralized span-range check. Accepts std::span<Byte>, std::span<const Byte>, and vBytes
// (all implicitly convert to std::span<const Byte>).
[[nodiscard]] inline bool spanHasRange(std::span<const Byte> data, std::size_t index, std::size_t length) {
	return index <= data.size() && length <= (data.size() - index);
}

inline void requireSpanRange(std::span<const Byte> data, std::size_t index, std::size_t length, std::string_view message) {
	if (!spanHasRange(data, index, length)) {
		throw std::runtime_error(std::string(message));
	}
}

[[nodiscard]] inline std::size_t checkedAddSize(std::size_t lhs, std::size_t rhs, std::string_view message) {
	if (lhs > std::numeric_limits<std::size_t>::max() - rhs) {
		throw std::runtime_error(std::string(message));
	}
	return lhs + rhs;
}

[[nodiscard]] inline std::size_t checkedMulSize(std::size_t lhs, std::size_t rhs, std::string_view message) {
	if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
		throw std::runtime_error(std::string(message));
	}
	return lhs * rhs;
}

// Append `bytes` to `out`, growing it with an overflow-checked resize. `message`
// is the error thrown if the combined size would overflow std::size_t.
void appendBytes(vBytes& out, std::span<const Byte> bytes, std::string_view message);

void closeFdNoThrow(int& fd) noexcept;
void closeFdOrThrow(int& fd);
void writeAllToFd(int fd, std::span<const Byte> data);
void cleanupPathNoThrow(const fs::path& path) noexcept;
