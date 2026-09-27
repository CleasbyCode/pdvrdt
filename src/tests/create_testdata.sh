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

# --- Cover PNGs ------------------------------------------------------------
# RGBA gradients with well over 256 distinct colours, so optimizeImage keeps
# them truecolor (exercising the strip-and-copy path rather than palette
# conversion). Emitted via the vendored lodepng so the fixtures need no
# external image tooling.
#
# The two sizes are chosen for their Reddit *carrier domain* parity, not
# arbitrarily. The carrier permutes sample positions with a Feistel network
# over the smallest power-of-two domain holding the RGB sample count, and an
# odd bit length there leaves the two Feistel halves unequal -- a distinct code
# path, and under carrier scheme 1 a different permutation entirely:
#
#   cover.png             128x128 -> 49152 samples -> 16 bits (even)
#   cover_odd_domain.png  150x150 -> 67500 samples -> 17 bits (odd)
#
# Only cover.png existed when the scheme-1 permutation was replaced, so the
# whole suite stayed green while every odd-domain carrier position moved. The
# parity check at the end of this script asserts both, so regenerating either
# fixture at other dimensions fails loudly instead of quietly retiring the
# coverage.
make_cover() {
    local out="$1" w="$2" h="$3"
    if [[ -f "$out" ]]; then
        return 0
    fi
    if [[ ! -f "$SRC/lodepng/lodepng.cpp" ]]; then
        echo "Cannot locate vendored lodepng at $SRC/lodepng/lodepng.cpp" >&2
        exit 1
    fi
    local tmp
    tmp="$(mktemp -d)"
    cat > "$tmp/make_cover.cpp" <<'CPP'
#include "lodepng/lodepng.h"
#include <cstddef>
#include <cstdlib>
#include <vector>
int main(int argc, char** argv) {
    if (argc != 4) return 1;
    const unsigned w = static_cast<unsigned>(std::atoi(argv[2]));
    const unsigned h = static_cast<unsigned>(std::atoi(argv[3]));
    std::vector<unsigned char> img(static_cast<std::size_t>(w) * h * 4);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            img[i + 0] = static_cast<unsigned char>((x * 3 + y) & 0xFF);
            img[i + 1] = static_cast<unsigned char>((y * 5 + x) & 0xFF);
            img[i + 2] = static_cast<unsigned char>((x + y * 2) & 0xFF);
            img[i + 3] = 255;
        }
    return lodepng_encode32_file(argv[1], img.data(), w, h);
}
CPP
    (
        cd "$tmp"
        g++ -std=c++23 -O2 -I"$SRC" make_cover.cpp "$SRC/lodepng/lodepng.cpp" -o make_cover
        ./make_cover cover.png "$w" "$h"
    )
    cp "$tmp/cover.png" "$out"
    rm -rf "$tmp"
}

make_cover "$DATA/covers/cover.png" 128 128
make_cover "$DATA/covers/cover_odd_domain.png" 150 150

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
    # Sized to sit well inside cover_odd_domain.png's ~2230-byte carrier
    # envelope while still spanning ~3000 nibble groups, so the odd-domain
    # round trip drives the permutation across a payload rather than only
    # across the 576-slot header.
    "payload_odd.bin":   (1_500, 45),
}
for name, (size, seed) in specs.items():
    p = root / name
    if p.exists() and p.stat().st_size == size:
        continue
    rng = random.Random(seed)
    p.write_bytes(bytes(rng.randrange(256) for _ in range(size)))
PY

# --- Carrier-domain parity check ------------------------------------------
# The cover comment above states which fixture covers which Feistel-half
# geometry. Verify it rather than trust it: these dimensions are load-bearing.
DATA="$DATA" python3 - <<'PARITY'
import os, struct, sys
from pathlib import Path

covers = Path(os.environ["DATA"]) / "covers"
expected = {"cover.png": "even", "cover_odd_domain.png": "odd"}
wrong = []
for name, want in sorted(expected.items()):
    width, height = struct.unpack(">II", (covers / name).read_bytes()[16:24])
    samples = width * height * 3
    bits = (samples - 1).bit_length()
    got = "even" if bits % 2 == 0 else "odd"
    if got != want:
        wrong.append(name)
    print(f"  {name}: {width}x{height}, {samples} samples, "
          f"{bits}-bit {got} carrier domain "
          f"({'ok' if got == want else 'WRONG, wanted ' + want})")
if wrong:
    print("Cover dimensions no longer provide both carrier-domain parities: "
          + ", ".join(wrong), file=sys.stderr)
    sys.exit(1)
PARITY

echo "Testdata ready under: $DATA"
