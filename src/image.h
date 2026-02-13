#pragma once

#include "common.h"

struct ImageCheckResult {
	bool has_bad_dims = false;
};

[[nodiscard]] ImageCheckResult optimizeImage(vBytes& image_file_vec);
