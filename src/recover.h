#pragma once

#include "common.h"

// Recover from the image file at `image_path`. A default-mode payload is read
// from the file frame by frame, never whole; any other image (Mastodon, Reddit,
// or anything the chunk walk does not accept) is read whole, as before.
void recoverDataFromFile(const fs::path& image_path);
