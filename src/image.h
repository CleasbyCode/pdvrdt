#pragma once

#include "common.h"

#include <span>

// Reject a PNG whose declared IHDR geometry would inflate past the decoder's
// safety ceiling, before lodepng allocates a pixel buffer for it. Every path
// that hands untrusted bytes to lodepng must call this first -- conceal does so
// via optimizeImage(), recover via extractRedditPngPayload().
void preflightPngDecode(std::span<const Byte> png);

[[nodiscard]] bool optimizeImage(vBytes& image_file_vec);

// Validation-only cover preparation for the Reddit carrier.
//
// Applies the same cover policy as optimizeImage() -- geometry bound, chunk
// validation, APNG rejection -- and strips any stale pdvrdt IDAT payload from a
// reused cover, then stops. It deliberately does NOT decode, palettize or
// re-encode: the carrier decodes the cover itself, so everything optimizeImage()
// builds past this point is discarded unread. On a low-colour cover that
// discarded work is a full palette conversion plus a complete PNG re-encode.
void prepareCoverForRedditCarrier(vBytes& image_file_vec);

void prepareImageForMastodonEmbedding(vBytes& image_file_vec);
