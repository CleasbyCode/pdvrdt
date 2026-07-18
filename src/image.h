#pragma once

#include "common.h"

[[nodiscard]] bool optimizeImage(vBytes& image_file_vec);
void prepareImageForMastodonEmbedding(vBytes& image_file_vec);
