// Compile lodepng with pdvrdt's configuration.
// This wrapper ensures the feature toggles are applied before lodepng.h is included.
// Compile this file instead of lodepng/lodepng.cpp directly.

#include "lodepng_config.h"

// Suppress warnings from vendored third-party code.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#include "lodepng.cpp"
#pragma GCC diagnostic pop
