#pragma once

#include "common.h"

enum class FileTypeCheck : Byte {
	cover_image    = 1,
	embedded_image = 2,
	data_file      = 3
};

[[nodiscard]] bool hasValidFilename(const fs::path& p);
[[nodiscard]] bool hasFileExtension(const fs::path& p, std::initializer_list<std::string_view> exts);
[[nodiscard]] vBytes readFile(const fs::path& path, FileTypeCheck file_type = FileTypeCheck::data_file);
