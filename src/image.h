#pragma once

#include "common.h"

#include <span>

// Largest cover image dimension, in pixels, each conceal mode accepts. Neither
// side may exceed the limit for the mode in use. The default and Mastodon modes
// stop where the platforms themselves do; the Reddit carrier hides its payload
// in the pixels, so its capacity grows with dimensions and it is given the
// larger bound.
inline constexpr std::uint32_t
	MAX_COVER_DIMENSION        = 4096,
	MAX_REDDIT_COVER_DIMENSION = 8192;

// Reject a PNG whose declared IHDR geometry breaches the cover-dimension limit
// for the mode in use, or would inflate past the decoder's safety ceiling,
// before lodepng allocates a pixel buffer for it. Every path that hands
// untrusted bytes to lodepng must call this first -- conceal does so via
// optimizeImage(), recover via extractRedditPngPayload().
void preflightPngDecode(std::span<const Byte> png, std::uint32_t max_cover_dimension);

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
