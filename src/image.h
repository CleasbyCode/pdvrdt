#pragma once

#include "common.h"

#include <span>

// Reject a PNG whose declared IHDR geometry would inflate past the decoder's
// safety ceiling, before lodepng allocates a pixel buffer for it. Every path
// that hands untrusted bytes to lodepng must call this first -- conceal does so
// via optimizeImage(), recover via extractRedditPngPayload().
void preflightPngDecode(std::span<const Byte> png);

[[nodiscard]] bool optimizeImage(vBytes& image_file_vec);
void prepareImageForMastodonEmbedding(vBytes& image_file_vec);
