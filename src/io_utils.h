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
[[nodiscard]] bool hasFileExtension(const fs::path& p, std::initializer_list<std::string_view> exts);
[[nodiscard]] std::size_t getFileSizeChecked(const fs::path& path, FileTypeCheck file_type = FileTypeCheck::data_file);
[[nodiscard]] vBytes readFile(const fs::path& path, FileTypeCheck file_type = FileTypeCheck::data_file);

// Centralized span-range check (works with std::span<T> or const vBytes&).
template <typename T>
[[nodiscard]] constexpr bool spanHasRange(std::span<T> data, std::size_t index, std::size_t length) {
	return index <= data.size() && length <= (data.size() - index);
}

[[nodiscard]] inline bool spanHasRange(const vBytes& data, std::size_t index, std::size_t length) {
	return index <= data.size() && length <= (data.size() - index);
}

inline void requireSpanRange(std::span<const Byte> data, std::size_t index, std::size_t length, std::string_view message) {
	if (!spanHasRange(data, index, length)) {
		throw std::runtime_error(std::string(message));
	}
}

inline void requireSpanRange(const vBytes& data, std::size_t index, std::size_t length, std::string_view message) {
	if (!spanHasRange(data, index, length)) {
		throw std::runtime_error(std::string(message));
	}
}

void closeFdNoThrow(int& fd) noexcept;
void closeFdOrThrow(int& fd);
void writeAllToFd(int fd, std::span<const Byte> data);
void cleanupPathNoThrow(const fs::path& path) noexcept;
