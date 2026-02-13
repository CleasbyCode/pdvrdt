// Compile lodepng with pdvrdt's configuration.
// This wrapper ensures the feature toggles are applied before lodepng.h is included.
// Compile this file instead of lodepng/lodepng.cpp directly.

#include "lodepng_config.h"
#include "lodepng.cpp"
