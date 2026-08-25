#!/bin/bash
# Create deterministic payloads and a cover PNG for pdvrdt golden-file tests.
#
# Dependencies: g++ (compiles the vendored lodepng to emit the cover, so no
# image tooling is required) and python3 (seeded, reproducible payloads).
set -euo pipefail

TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$(cd "$TESTS/.." && pwd)"
DATA="$TESTS/testdata"
mkdir -p "$DATA/covers" "$DATA/payloads"

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Missing required command: $1" >&2
        exit 1
    fi
}

need_cmd g++
need_cmd python3

# --- Cover PNG -------------------------------------------------------------
# 128x128 RGBA gradient. The gradient has well over 256 distinct colours, so
# optimizeImage keeps it truecolor (exercises the strip-and-copy path rather
# than palette conversion). Emitted via the vendored lodepng so the fixtures
# need no external image tooling.
COVER="$DATA/covers/cover.png"
if [[ ! -f "$COVER" ]]; then
    if [[ ! -f "$SRC/lodepng/lodepng.cpp" ]]; then
        echo "Cannot locate vendored lodepng at $SRC/lodepng/lodepng.cpp" >&2
        exit 1
    fi
    tmp="$(mktemp -d)"
    cat > "$tmp/make_cover.cpp" <<'CPP'
#include "lodepng/lodepng.h"
#include <cstddef>
#include <vector>
int main() {
    const unsigned w = 128, h = 128;
    std::vector<unsigned char> img(static_cast<std::size_t>(w) * h * 4);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            img[i + 0] = static_cast<unsigned char>((x * 3 + y) & 0xFF);
            img[i + 1] = static_cast<unsigned char>((y * 5 + x) & 0xFF);
            img[i + 2] = static_cast<unsigned char>((x + y * 2) & 0xFF);
            img[i + 3] = 255;
        }
    return lodepng_encode32_file("cover.png", img.data(), w, h);
}
CPP
    ( cd "$tmp" && g++ -std=c++23 -O2 -I"$SRC" make_cover.cpp "$SRC/lodepng/lodepng.cpp" -o make_cover && ./make_cover )
    cp "$tmp/cover.png" "$COVER"
    rm -rf "$tmp"
fi

# --- Deterministic payloads -----------------------------------------------
# Seeded RNG so bytes are reproducible across runs and machines. The ".gz"
# name (content is just random bytes) triggers pdvrdt's "already compressed"
# bypass, which stores the payload with Z_NO_COMPRESSION instead of deflating.
DATA="$DATA" python3 - <<'PY'
import os, random
from pathlib import Path

root = Path(os.environ["DATA"]) / "payloads"
root.mkdir(parents=True, exist_ok=True)

text = root / "payload_text.txt"
if not text.exists():
    text.write_bytes(
        b"pdvrdt golden test payload.\n"
        b"The quick brown fox jumps over the lazy dog.\n"
        b"Line three.\n"
    )

specs = {
    "payload_bin.bin":   (300_000, 42),
    "payload_mast.bin":  (40_000,  43),
    "payload_stored.gz": (50_000,  44),
}
for name, (size, seed) in specs.items():
    p = root / name
    if p.exists() and p.stat().st_size == size:
        continue
    rng = random.Random(seed)
    p.write_bytes(bytes(rng.randrange(256) for _ in range(size)))
PY

echo "Testdata ready under: $DATA"
