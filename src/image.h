#pragma once

#include "common.h"

struct ImageCheckResult {
	bool has_bad_dims = false;
};

constexpr Byte
	INDEXED_PLTE    = 3,
	TRUECOLOR_RGB   = 2,
	TRUECOLOR_RGBA  = 6;

[[nodiscard]] ImageCheckResult optimizeImage(vBytes& image_file_vec);
