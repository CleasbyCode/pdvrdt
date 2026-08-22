#pragma once

#include "common.h"

#include <cstring>
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

class OpenInputFile {
public:
	OpenInputFile() = default;
	OpenInputFile(const OpenInputFile&) = delete;
	OpenInputFile& operator=(const OpenInputFile&) = delete;
	OpenInputFile(OpenInputFile&& other) noexcept;
	OpenInputFile& operator=(OpenInputFile&& other) noexcept;
	~OpenInputFile();

	[[nodiscard]] int fd() const noexcept { return fd_; }
	[[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
	friend OpenInputFile openInputFile(const fs::path&, FileTypeCheck);
	explicit OpenInputFile(int fd, std::size_t size) noexcept : fd_(fd), size_(size) {}

	int fd_{-1};
	std::size_t size_{};
};

// Largest cover image conceal mode will accept, in bytes. Reported by --info and
// named in the rejection message so the rule is discoverable before it bites.
inline constexpr std::size_t MAX_COVER_IMAGE_SIZE = 8ULL * 1024 * 1024;

[[nodiscard]] bool hasValidFilename(const fs::path& p);
// hasValidFilename plus the reserved-name rules used for embedded/recovered
// filenames: rejects ".", "..", a leading '.' or '-', and a trailing space or '.'.
[[nodiscard]] bool hasSafeEmbeddedFilename(const fs::path& p);
// The specific reason `p` is unusable as an embedded filename, or an empty view
// if it is fine. Callers that report the failure to the user should use this
// rather than restating a partial version of the rules.
[[nodiscard]] std::string_view embeddedFilenameProblem(const fs::path& p);
[[nodiscard]] bool hasFileExtension(const fs::path& p, std::initializer_list<std::string_view> exts);
[[nodiscard]] OpenInputFile openInputFile(const fs::path& path, FileTypeCheck file_type = FileTypeCheck::data_file);
[[nodiscard]] vBytes readFile(const fs::path& path, FileTypeCheck file_type = FileTypeCheck::data_file);

// Centralized span-range check. Accepts std::span<Byte>, std::span<const Byte>, and vBytes
// (all implicitly convert to std::span<const Byte>).
[[nodiscard]] inline bool spanHasRange(std::span<const Byte> data, std::size_t index, std::size_t length) {
	return index <= data.size() && length <= (data.size() - index);
}

// Bounds-checked byte comparison at an offset: false rather than UB when the
// range does not fit.
[[nodiscard]] inline bool bytesEqualAt(std::span<const Byte> data, std::size_t index, std::span<const Byte> expected) {
	return spanHasRange(data, index, expected.size()) &&
		std::memcmp(data.data() + index, expected.data(), expected.size()) == 0;
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

// Flush `fd`'s contents to stable storage. Callers report success to the user
// only after this returns, so a write error deferred by the page cache (ENOSPC,
// EIO on a failing disk) surfaces as a failure rather than as a "Complete!" for
// a file that never landed.
void fsyncFdOrThrow(int fd);

// Flush the directory entry for `path`, so the name itself survives a crash and
// not just the bytes behind it. Best-effort and non-throwing: a directory that
// cannot be opened for reading (write+execute but not read) is a permissions
// quirk, not a sign that the data is at risk, and must not fail an operation
// that has otherwise fully succeeded.
void fsyncParentDirectoryNoThrow(const fs::path& path) noexcept;
void writeAllToFd(int fd, std::span<const Byte> data);
void cleanupPathNoThrow(const fs::path& path) noexcept;
