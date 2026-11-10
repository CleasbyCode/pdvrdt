#!/bin/bash
# Fresh conceal/recover round-trip regression tests for pdvrdt.
#
# These complement the golden recover fixtures by exercising the current
# conceal pipeline, parsing the generated recovery KEY/output image, recovering,
# and comparing the recovered payload bytes.
set -euo pipefail

TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$TESTS/.." && pwd)"
CALLER_PWD="$(pwd -P)"
BIN="${PDVRDT_BIN:-}"
NO_BUILD=0
[[ -n "$BIN" ]] && NO_BUILD=1
BUILD_DIR=""
WORK_ROOT=""

cleanup() {
    local status=$?
    [[ -z "$WORK_ROOT" ]] || rm -rf -- "$WORK_ROOT"
    [[ -z "$BUILD_DIR" ]] || rm -rf -- "$BUILD_DIR"
    trap - EXIT
    exit "$status"
}
trap cleanup EXIT

usage() {
    cat <<'EOF'
Usage: tests/run_roundtrip_tests.sh [options]

Options:
  --no-build    Reuse existing pdvrdt binary.
  --bin <path>  Use an explicit binary path.
  -h, --help    Show this help.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build) NO_BUILD=1; shift;;
        --bin)
            if [[ $# -lt 2 ]]; then
                echo "Option --bin requires a path." >&2
                usage >&2
                exit 2
            fi
            if [[ -z "$2" || "$2" == -* ]]; then
                echo "Option --bin requires a non-option path." >&2
                usage >&2
                exit 2
            fi
            BIN="$2"
            NO_BUILD=1
            shift 2
            ;;
        --bin=*)
            BIN="${1#*=}"
            if [[ -z "$BIN" ]]; then
                echo "Option --bin requires a path." >&2
                usage >&2
                exit 2
            fi
            NO_BUILD=1
            shift
            ;;
        -h|--help) usage; exit 0;;
        *) echo "Unknown option: $1" >&2; usage; exit 2;;
    esac
done

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Missing required command: $1" >&2
        exit 1
    fi
}

need_cmd cmp
need_cmd grep
need_cmd od
need_cmd sed

if [[ -n "$BIN" && "$BIN" != /* ]]; then
    BIN="$CALLER_PWD/${BIN#./}"
fi

if [[ "$NO_BUILD" -eq 0 ]]; then
    BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/pdvrdt-roundtrip-build.XXXXXX")"
    BIN="$BUILD_DIR/pdvrdt"
    (cd "$ROOT" && PDVRDT_OUTPUT="$BIN" bash ./compile_pdvrdt.sh)
elif [[ -z "$BIN" ]]; then
    BIN="$ROOT/pdvrdt"
fi

if [[ ! -x "$BIN" ]]; then
    echo "Binary not found or not executable: $BIN" >&2
    exit 1
fi

extract_embedded_image() {
    sed -n 's/.*Saved "file-embedded" PNG image: \([^ ]*\) (.*/\1/p' "$1" | tail -n 1
}

extract_key() {
    sed -nE 's/.*Recovery KEY: \[\*\*\*([1-9A-HJ-NP-Za-km-z]{4}(-[1-9A-HJ-NP-Za-km-z]{4}){5})\*\*\*\].*/\1/p' "$1" | tail -n 1
}

extract_recovered_file() {
    sed -n '/^Writing output file\.\.\.$/{n;s/^ ✓ \([^ ]*\) (.*/\1/p;}' "$1" | tail -n 1
}

PASS=0
FAIL=0

run_capsize_case() {
    local case_id="capsize_reddit"
    local cover="$TESTS/testdata/covers/cover.png"
    local work="$WORK_ROOT/$case_id"

    rm -rf "$work"
    mkdir -p "$work"
    cp "$cover" "$work/cover.png"

    pushd "$work" >/dev/null
    if ! "$BIN" capsize cover.png > capsize.log 2>&1; then
        popd >/dev/null
        echo "[FAIL] $case_id: capsize command failed" >&2
        cat "$work/capsize.log" >&2
        return 1
    fi

    # The header and steps, in order, then the closing note.
    local expected_head
    expected_head=$(printf '%s\n' \
        "" \
        "Cover image: cover.png (883 bytes)." \
        "Capacity check for conceal -r (Reddit) mode only." \
        "" \
        "Preparing cover image..." \
        " ✓ 128x128, 8-bit RGB, metadata removed." \
        "Measuring carrier capacity..." \
        " ✓ 1619 bytes (1.6KiB), adaptive (1,15,4) matrix embedding." \
        "Calculating payload limits..." \
        " ✓ 1401 bytes (1.4KiB) conservative maximum, 377 bytes recommended, for a 20-byte filename." \
        "" \
        "Important:")
    if [[ "$(head -n 12 capsize.log)" != "$expected_head" ]]; then
        popd >/dev/null
        echo "[FAIL] $case_id: capsize header or steps differ" >&2
        diff <(printf '%s\n' "$expected_head") <(head -n 12 "$work/capsize.log") >&2
        return 1
    fi
    if ! grep -Fqx -- "The final embedded PNG size can differ from the cover image and must remain within 20MiB." capsize.log; then
        popd >/dev/null
        echo "[FAIL] $case_id: missing closing note" >&2
        cat "$work/capsize.log" >&2
        return 1
    fi
    if compgen -G 'prdt_*.png' >/dev/null; then
        popd >/dev/null
        echo "[FAIL] $case_id: capsize unexpectedly saved an image" >&2
        return 1
    fi

    popd >/dev/null
    echo "[PASS] $case_id"
    return 0
}

run_reddit_capacity_failure_case() {
    local case_id="reddit_capacity_failure"
    local cover="$TESTS/testdata/covers/cover.png"
    local payload="$TESTS/testdata/payloads/payload_stored.gz"
    local work="$WORK_ROOT/$case_id"

    rm -rf "$work"
    mkdir -p "$work"
    cp "$cover" "$work/cover.png"
    cp "$payload" "$work/payload_stored.gz"

    pushd "$work" >/dev/null
    if "$BIN" conceal -r cover.png payload_stored.gz > conceal.log 2>&1; then
        popd >/dev/null
        echo "[FAIL] $case_id: oversized Reddit payload unexpectedly succeeded" >&2
        return 1
    fi

	local required_lines=(
		"Cover image: cover.png (883 bytes)."
		"Data file:   payload_stored.gz (48KiB)."
		"Preparing cover image..."
		" ✓ 128x128, 8-bit RGB, metadata removed."
		"Compressing data file..."
		" ✓ skipped (file header shows gzip data, already compressed)."
		"Checking cover capacity..."
		" ✗ does not fit."
		"Stored payload size:                                     50011 bytes (level-0 zlib stream of the 50000-byte file; its header shows gzip data, already compressed)."
		"Encryption and carrier overhead:                         215 bytes (17-byte filename)."
		"Total encrypted profile size:                            50226 bytes."
		"Theoretical adaptive capacity of this cover image:       1619 bytes."
		"Maximum stored payload size for this filename:           1404 bytes (~1KiB)."
		"Recommended stored payload size:                         380 bytes (~0KiB, keeps 1KiB headroom)."
		"Data File Size Error: Stored payload of 50011 bytes exceeds this cover image's capacity by 48607 bytes."
		"                      Use a smaller data file or a larger cover image."
    )
    local expected
    for expected in "${required_lines[@]}"; do
        if ! grep -Fqx -- "$expected" conceal.log; then
            popd >/dev/null
            echo "[FAIL] $case_id: missing output line: $expected" >&2
            cat "$work/conceal.log" >&2
            return 1
        fi
    done
    if compgen -G 'prdt_*.png' >/dev/null; then
        popd >/dev/null
        echo "[FAIL] $case_id: failed conceal published an image" >&2
        return 1
    fi

    popd >/dev/null
    echo "[PASS] $case_id"
    return 0
}

# Whether a payload is stored rather than deflated is decided by its first
# bytes, not its name. The same 50000 bytes: with the gzip signature under a
# ".txt" name they are stored; random bytes under a ".gz" name are deflated
# (libdeflate and miniz size that differently, so only the note is checked).
run_compression_header_case() {
    local case_id="compression_by_header"
    local work="$WORK_ROOT/$case_id"
    local signed="$TESTS/testdata/payloads/payload_stored.gz"

    rm -rf "$work"
    mkdir -p "$work"
    cp "$TESTS/testdata/covers/cover.png" "$work/cover.png"
    cp "$signed" "$work/signed_gzip.txt"
    # Clear the signature: 00 00 matches no format.
    python3 -c 'import sys; d = bytearray(open(sys.argv[1], "rb").read()); d[0:2] = b"\0\0"; open(sys.argv[2], "wb").write(d)' \
        "$signed" "$work/unsigned.gz"

    pushd "$work" >/dev/null
    local name expected
    for name in signed_gzip.txt unsigned.gz; do
        if "$BIN" conceal -r cover.png "$name" > "$name.log" 2>&1; then
            popd >/dev/null
            echo "[FAIL] $case_id: oversized Reddit payload $name unexpectedly succeeded" >&2
            return 1
        fi
    done
    expected="Stored payload size:                                     50011 bytes (level-0 zlib stream of the 50000-byte file; its header shows gzip data, already compressed)."
    if ! grep -Fqx -- "$expected" signed_gzip.txt.log; then
        popd >/dev/null
        echo "[FAIL] $case_id: a gzip-signed .txt file was not stored as already compressed" >&2
        cat "$work/signed_gzip.txt.log" >&2
        return 1
    fi
    if grep -Fq "already compressed" unsigned.gz.log ||
       ! grep -Eq "^Stored payload size: +[0-9]+ bytes \(zlib stream of the 50000-byte file; compression did not reduce it\)\.$" unsigned.gz.log; then
        popd >/dev/null
        echo "[FAIL] $case_id: an unsigned .gz file was not deflated" >&2
        cat "$work/unsigned.gz.log" >&2
        return 1
    fi
    popd >/dev/null
    echo "[PASS] $case_id"
    return 0
}

# The Reddit carrier decodes covers that cannot hold transparency straight to
# RGB and everything else through RGBA, checking every pixel. Exercise both:
# opaque covers of each kind round-trip, and any visible transparency --
# alpha, a used tRNS palette entry, a used tRNS key colour -- is refused.
run_reddit_cover_types_case() {
    local case_id="reddit_cover_types"
    local work="$WORK_ROOT/$case_id"
    rm -rf "$work"
    mkdir -p "$work"
    cp "$TESTS/testdata/payloads/payload_text.txt" "$work/payload.txt"
    python3 - "$work" <<'PY'
import struct, sys, zlib
from pathlib import Path
out = Path(sys.argv[1])
W = H = 128
def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
def png(name, colour, rows, extra=b""):
    ihdr = struct.pack(">IIBBBBB", W, H, 8, colour, 0, 0, 0)
    raw = b"".join(b"\0" + r for r in rows)
    (out / name).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + extra +
                             chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
def px(x, y):
    return ((x * 7 + y * 3) & 0xFF, (x * 5 + y * 11) & 0xFF, (x * 13 + y) & 0xFF)
rgb_rows = [b"".join(bytes(px(x, y)) for x in range(W)) for y in range(H)]
rgba = lambda clear: [b"".join(bytes(px(x, y)) + bytes([0 if (clear and x == 5 and y == 9) else 255])
                               for x in range(W)) for y in range(H)]
png("ok_rgba.png", 6, rgba(False))
png("bad_rgba.png", 6, rgba(True))
palette = b"".join(bytes(((i * 37) & 0xFF, (i * 91) & 0xFF, (i * 53) & 0xFF)) for i in range(256))
pal_rows = [bytes(((x * 3 + y) & 0xFF) for x in range(W)) for y in range(H)]
png("ok_palette.png", 3, pal_rows, chunk(b"PLTE", palette))
png("bad_palette.png", 3, pal_rows, chunk(b"PLTE", palette) + chunk(b"tRNS", b"\xff" * 7 + b"\x00"))
used = px(5, 9)
png("bad_rgb_key.png", 2, rgb_rows, chunk(b"tRNS", struct.pack(">HHH", *used)))
png("ok_rgb_key.png", 2, rgb_rows, chunk(b"tRNS", struct.pack(">HHH", 1, 2, 3)))
# 16-bit alpha must be checked at 16 bits: 0xFFFE is not opaque, although its
# high byte is 0xFF.
def png16(name, colour, rows):
    ihdr = struct.pack(">IIBBBBB", W, H, 16, colour, 0, 0, 0)
    raw = b"".join(b"\0" + r for r in rows)
    (out / name).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
                             chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
for alpha, tag in ((0xFFFF, "ok"), (0xFFFE, "bad")):
    png16(f"{tag}_rgba16.png", 6, [b"".join(struct.pack(">HHHH", *(v * 257 for v in px(x, y)), alpha)
                                           for x in range(W)) for y in range(H)])
    png16(f"{tag}_ga16.png", 4, [b"".join(struct.pack(">HH", px(x, y)[0] * 257, alpha)
                                         for x in range(W)) for y in range(H)])
PY
    pushd "$work" >/dev/null
    local cover image key recovered
    for cover in ok_rgba.png ok_palette.png ok_rgb_key.png ok_rgba16.png ok_ga16.png; do
        mkdir -p "run_$cover" && cp "$cover" payload.txt "run_$cover/"
        pushd "run_$cover" >/dev/null
        if ! "$BIN" conceal -r "$cover" payload.txt > conceal.log 2>&1; then
            popd >/dev/null; popd >/dev/null
            echo "[FAIL] $case_id: opaque cover $cover was refused" >&2
            cat "$work/run_$cover/conceal.log" >&2
            return 1
        fi
        image="$(extract_embedded_image conceal.log)"
        key="$(extract_key conceal.log)"
        mkdir out && cp "$image" out/in.png
        if ! (cd out && printf '%s\n' "$key" | "$BIN" recover in.png > recover.log 2>&1) ||
           ! cmp -s out/payload.txt payload.txt; then
            popd >/dev/null; popd >/dev/null
            echo "[FAIL] $case_id: $cover did not round-trip" >&2
            return 1
        fi
        popd >/dev/null
    done
    for cover in bad_rgba.png bad_palette.png bad_rgb_key.png bad_rgba16.png bad_ga16.png; do
        if "$BIN" conceal -r "$cover" payload.txt > "$cover.log" 2>&1 ||
           ! grep -Fq "Reddit cover contains transparency" "$cover.log" ||
           "$BIN" capsize "$cover" > "$cover.capsize.log" 2>&1 ||
           ! grep -Fq "Reddit cover contains transparency" "$cover.capsize.log"; then
            popd >/dev/null
            echo "[FAIL] $case_id: transparent cover $cover was not refused" >&2
            cat "$work/$cover.log" >&2
            return 1
        fi
    done
    popd >/dev/null
    echo "[PASS] $case_id"
    return 0
}

# Conceal reports each step on two lines: "Label..." then " ✓ result". Byte
# counts that depend on the compressor (they differ between the C++ and Rust
# builds) are matched by pattern; everything else exactly.
run_conceal_step_output_case() {
    local case_id="conceal_step_output"
    local work="$WORK_ROOT/$case_id"
    rm -rf "$work"
    mkdir -p "$work"
    cp "$TESTS/testdata/covers/cover.png" "$work/cover.png"
    cp "$TESTS/testdata/payloads/payload_text.txt" "$work/payload_text.txt"
    pushd "$work" >/dev/null
    local mode log
    local encrypting=" ✓ XChaCha20-Poly1305, 256-bit key derived by Argon2id from the 128-bit recovery KEY."
    for mode in default -m -r; do
        log="conceal_${mode#-}.log"
        local option=()
        [[ "$mode" != default ]] && option=("$mode")
        if ! "$BIN" conceal "${option[@]}" cover.png payload_text.txt > "$log" 2>&1; then
            popd >/dev/null
            echo "[FAIL] $case_id: conceal $mode failed" >&2
            cat "$work/$log" >&2
            return 1
        fi
        local exact=(
            "Cover image: cover.png (883 bytes)."
            "Data file:   payload_text.txt (85 bytes)."
            "Preparing cover image..."
            "Compressing data file..."
            "Encrypting..."
            "$encrypting"
            "Embedding..."
            "Writing output image..."
        )
        local patterns=('^ ✓ 85 bytes -> [0-9]+ bytes \((no reduction|[0-9]+% smaller)\)\.$')
        case "$mode" in
            default)
                exact+=(" ✓ 128x128, 8-bit RGB, color metadata kept, other metadata removed.")
                patterns+=('^ ✓ Final IDAT chunk \([0-9]+ bytes\)\.$' '^ ✓ [0-9.]+KiB\.$')
                ;;
            -m)
                exact+=(" ✓ 128x128, 8-bit RGB, color metadata kept (iCCP/sRGB removed), other metadata removed.")
                patterns+=('^ ✓ iCCP chunk \([0-9]+ bytes\)\.$'
                    "^ ✓ [0-9.]+KiB \(<1% of Mastodon's 16 MiB upload size limit\)\.$")
                ;;
            -r)
                exact+=(" ✓ 128x128, 8-bit RGB, metadata removed."
                    "Checking cover capacity..."
                    " ✓ Adaptive (1,15,4) matrix embedding --> Least significant bits of keyed RGB samples."
                    " ✓ Reddit and Mastodon.")
                patterns+=('^ ✓ [0-9]+ of 1619 bytes \([0-9]+%\)\.$'
                    "^ ✓ [0-9.]+KiB \(<1% of Reddit's 20 MiB upload size limit\)\.$")
                ;;
        esac
        local line
        for line in "${exact[@]}"; do
            if ! grep -Fqx -- "$line" "$log"; then
                popd >/dev/null
                echo "[FAIL] $case_id: conceal $mode is missing: $line" >&2
                cat "$work/$log" >&2
                return 1
            fi
        done
        for line in "${patterns[@]}"; do
            if ! grep -Eq -- "$line" "$log"; then
                popd >/dev/null
                echo "[FAIL] $case_id: conceal $mode has no line matching: $line" >&2
                cat "$work/$log" >&2
                return 1
            fi
        done
        # Steps come first, then the platform list, the image name and the KEY.
        if ! awk '/^Writing output image\.\.\.$/ { w = NR } /^Platform compatibility/ { p = NR }
                /^Saved "file-embedded" PNG image:/ { s = NR } /^Recovery KEY:/ { k = NR }
                END { exit !(w && w < p && p < s && s < k) }' "$log"; then
            popd >/dev/null
            echo "[FAIL] $case_id: conceal $mode output is out of order" >&2
            cat "$work/$log" >&2
            return 1
        fi
        if grep -Fq "Please wait" "$log"; then
            popd >/dev/null
            echo "[FAIL] $case_id: conceal $mode printed a wait note to a non-terminal" >&2
            return 1
        fi
        rm -f prdt_*.png
    done
    popd >/dev/null
    echo "[PASS] $case_id"
    return 0
}

# A -r image also suits Mastodon only within 16 MiB and 3840x2160 worth of
# pixels; one pixel column more falls back to the Reddit-only line.
run_reddit_mastodon_pixel_limit_case() {
    local case_id="reddit_mastodon_pixel_limit"
    local work="$WORK_ROOT/$case_id"
    rm -rf "$work"
    mkdir -p "$work"
    cp "$TESTS/testdata/payloads/payload_text.txt" "$work/payload_text.txt"
    python3 - "$work" <<'PY'
import struct, sys, zlib
def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
for name, width in (("at_limit.png", 3840), ("over_limit.png", 3841)):
    height = 2160
    row = bytes((x * 7) % 200 + 20 for x in range(width * 3))
    raw = b"".join(b"\0" + row for _ in range(height))
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(f"{sys.argv[1]}/{name}", "wb") as out:
        out.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
                  + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
PY
    pushd "$work" >/dev/null
    local cover expected
    for cover in at_limit.png over_limit.png; do
        if [[ "$cover" == at_limit.png ]]; then
            expected=" ✓ Reddit and Mastodon."
        else
            expected=' ✓ Reddit. (Only share this "file-embedded" PNG image on Reddit).'
        fi
        if ! "$BIN" conceal -r "$cover" payload_text.txt > "$cover.log" 2>&1; then
            popd >/dev/null
            echo "[FAIL] $case_id: conceal -r $cover failed" >&2
            cat "$work/$cover.log" >&2
            return 1
        fi
        if ! grep -A2 -F "Platform compatibility for output image:-" "$cover.log" | grep -Fqx -- "$expected"; then
            popd >/dev/null
            echo "[FAIL] $case_id: $cover should report: $expected" >&2
            cat "$work/$cover.log" >&2
            return 1
        fi
        rm -f prdt_*.png
    done
    popd >/dev/null
    echo "[PASS] $case_id"
    return 0
}

# A -m image's ICC profile must be one that PNG decoders accept, or software
# that validates it (libpng drops it) loses the payload with it: the declared
# profile size must be the profile's real length, for short and long payloads,
# and a greyscale cover must become truecolor, since the profile is RGB. The
# greyscale pixels, tRNS key and sBIT must carry over unchanged in meaning.
run_mastodon_icc_profile_case() {
    local case_id="mastodon_icc_profile"
    local work="$WORK_ROOT/$case_id"
    rm -rf "$work"
    mkdir -p "$work"
    printf 'hello world' > "$work/short.txt"
    cp "$TESTS/testdata/payloads/payload_mast.bin" "$work/long.bin"
    cp "$TESTS/testdata/covers/cover.png" "$work/rgb.png"
    python3 - "$work" <<'PY'
import struct, sys, zlib
from pathlib import Path
out = Path(sys.argv[1]); W = H = 100
def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
def png(name, depth, colour, rows, extra=b""):
    ihdr = struct.pack(">IIBBBBB", W, H, depth, colour, 0, 0, 0)
    raw = b"".join(b"\0" + r for r in rows)
    (out / name).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + extra +
                             chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
grey = lambda x, y: (x * 7 + y * 3) & 0xFF
png("grey8.png", 8, 0, [bytes(grey(x, y) for x in range(W)) for y in range(H)],
    chunk(b"sBIT", b"\x07"))
png("grey4_trns.png", 4, 0, [bytes(((x + y) % 16) << 4 | ((x + y + 1) % 16) for x in range(W // 2))
                             for y in range(H)], chunk(b"tRNS", struct.pack(">H", 3)))
png("grey_alpha16.png", 16, 4, [b"".join(struct.pack(">HH", grey(x, y) * 251, 40000 + x)
                                         for x in range(W)) for y in range(H)])
PY
    pushd "$work" >/dev/null
    local cover payload log image key
    for cover in rgb.png grey8.png grey4_trns.png grey_alpha16.png; do
        for payload in short.txt long.bin; do
            log="$cover.$payload.log"
            rm -f prdt_*.png
            if ! "$BIN" conceal -m "$cover" "$payload" > "$log" 2>&1; then
                popd >/dev/null
                echo "[FAIL] $case_id: conceal -m $cover $payload failed" >&2
                cat "$work/$log" >&2
                return 1
            fi
            image="$(extract_embedded_image "$log")"
            key="$(extract_key "$log")"
            if ! python3 - "$cover" "$image" <<'PY'
import struct, sys, zlib
def chunks(path):
    data = open(path, "rb").read(); at = 8
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        yield kind, data[at + 8:at + 8 + length]
        at += 12 + length
def pixels(path):
    # Every sample as a 0..65535 value, alpha last: what a decoder shows.
    found = dict(); idat = b""
    for kind, body in chunks(path):
        if kind == b"IDAT": idat += body
        else: found.setdefault(kind, body)
    w, h, depth, colour = struct.unpack(">IIBB", found[b"IHDR"][:10])
    channels = {0: 1, 2: 3, 4: 2, 6: 4}[colour]
    stride = (w * channels * depth + 7) // 8; bpp = max(1, channels * depth // 8)
    raw = zlib.decompress(idat); prev = bytearray(stride); rows = []
    for y in range(h):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0; b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
            p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
            line[i] = (line[i] + [0, a, b, (a + b) // 2, a if pa <= pb and pa <= pc else b if pb <= pc else c][kind]) & 0xFF
        rows.append(bytes(line)); prev = line
    key = found.get(b"tRNS"); maximum = (1 << depth) - 1; result = []
    for line in rows:
        for x in range(w):
            if depth < 8:
                samples = [(line[x * depth // 8] >> (8 - depth - x * depth % 8)) & maximum]
            elif depth == 8:
                samples = list(line[x * channels:(x + 1) * channels])
            else:
                samples = list(struct.unpack(f">{channels}H", line[x * channels * 2:(x + 1) * channels * 2]))
            raw_colour = samples[:3] if colour in (2, 6) else samples[:1]
            colour_out = [v * 65535 // maximum for v in raw_colour] * (1 if colour in (2, 6) else 3)
            if colour in (4, 6):
                alpha = samples[-1] * 65535 // maximum
            elif key is not None and raw_colour == list(struct.unpack(f">{len(key) // 2}H", key)):
                alpha = 0
            else:
                alpha = 65535
            result.append((*colour_out, alpha))
    return found, result
cover_chunks, cover_pixels = pixels(sys.argv[1])
image_chunks, image_pixels = pixels(sys.argv[2])
ihdr = image_chunks[b"IHDR"]
if ihdr[9] not in (2, 3, 6):
    sys.exit(f"output colour type {ihdr[9]} cannot carry an RGB profile")
body = image_chunks[b"iCCP"]; profile = zlib.decompress(body[body.index(0) + 2:])
declared = struct.unpack(">I", profile[:4])[0]
if declared != len(profile):
    sys.exit(f"ICC profile declares {declared} bytes but holds {len(profile)}")
if profile[16:20] != b"RGB ":
    sys.exit("ICC profile colour space is not RGB")
if cover_pixels != image_pixels:
    sys.exit("output pixels differ from the cover's")
if b"sBIT" in cover_chunks and image_chunks.get(b"sBIT") != cover_chunks[b"sBIT"][:1] * 3:
    sys.exit(f"sBIT not widened: {image_chunks.get(b'sBIT')!r}")
PY
            then
                popd >/dev/null
                echo "[FAIL] $case_id: conceal -m $cover $payload wrote an invalid profile or image" >&2
                return 1
            fi
            mkdir -p "out_$log" && cp "$image" "out_$log/in.png"
            if ! (cd "out_$log" && printf '%s\n' "$key" | "$BIN" recover in.png > recover.log 2>&1) ||
               ! cmp -s "out_$log/$payload" "$payload"; then
                popd >/dev/null
                echo "[FAIL] $case_id: $cover $payload did not round-trip" >&2
                return 1
            fi
        done
    done
    rm -f prdt_*.png
    popd >/dev/null
    echo "[PASS] $case_id"
    return 0
}

# Breaking the stdout pipe once the image is being written must never leave an
# image whose KEY was not shown: the write fails, the image is removed and no
# SIGPIPE kills the process first. stdout is unbuffered (stdbuf) so each report
# line is a write of its own.
run_broken_stdout_after_publish_case() {
    local case_id="broken_stdout_after_publish"
    if ! command -v stdbuf >/dev/null 2>&1; then
        echo "[SKIP] $case_id: stdbuf is not available"
        return 0
    fi
    local work="$WORK_ROOT/$case_id"
    rm -rf "$work"
    mkdir -p "$work"
    if ! python3 - "$BIN" "$TESTS/testdata" "$work" <<'PY'
import glob, os, subprocess, sys
binary, data, work = sys.argv[1:4]
for mode in ([], ["-m"], ["-r"]):
    for _ in range(3):
        process = subprocess.Popen(
            ["stdbuf", "-o0", binary, "conceal", *mode, f"{data}/covers/cover.png",
             f"{data}/payloads/payload_text.txt"],
            cwd=work, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        for line in process.stdout:
            if line.startswith(b"Writing output image..."):
                break
        process.stdout.close()
        status = process.wait()
        if status == -13:
            sys.exit(f"conceal {mode} was killed by SIGPIPE")
        if status == 0:
            sys.exit(f"conceal {mode} reported success with stdout closed")
        if glob.glob(os.path.join(work, "prdt_*.png")):
            sys.exit(f"conceal {mode} left an image whose KEY was not shown")
PY
    then
        echo "[FAIL] $case_id" >&2
        return 1
    fi
    echo "[PASS] $case_id"
    return 0
}

# An already-compressed (stored) payload's encrypted size is known before any
# work, so one that cannot fit the 2 GiB output limit must fail at once, even
# under an address-space limit, rather than allocating and encrypting ~2 GiB
# first; and one that fits must be written without holding its ciphertext in
# memory. Sparse files keep the fixtures cheap.
run_large_stored_payload_case() {
    local case_id="large_stored_payload"
    local work="$WORK_ROOT/$case_id"
    rm -rf "$work"
    mkdir -p "$work"
    if ! python3 - "$BIN" "$TESTS/testdata/covers/cover.png" "$work" <<'PY'
import glob, os, resource, subprocess, sys, time
binary, cover, work = sys.argv[1:4]
def sparse_zip(name, size):
    path = os.path.join(work, name)
    with open(path, "wb") as out:
        out.write(b"PK\x03\x04" + b"\x14" * 26)
        out.truncate(size)
    return path
def run(payload, limit_as=None, stdin=None):  # -> (exit code, output, seconds)
    def limit():
        if limit_as:
            resource.setrlimit(resource.RLIMIT_AS, (limit_as, limit_as))
    start = time.monotonic()
    process = subprocess.Popen([binary, *payload], cwd=work, stdin=subprocess.PIPE if stdin else None,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, preexec_fn=limit)
    output, _ = process.communicate(stdin)
    return process.returncode, output.decode(errors="replace"), time.monotonic() - start
asan = "libasan" in subprocess.run(["ldd", binary], capture_output=True, text=True).stdout
two_gib = 2 * 1024 ** 3
# Too large outright (the conceal-level check), and just too large once the
# encryption overhead is added (the encryption-level check).
for name, size, expected in (("over.zip", two_gib + (64 << 20), "cannot be reduced to fit the 2 GiB output size limit"),
                             ("edge.zip", two_gib - os.path.getsize(cover) - 15 - 4096,
                              "exceeds the selected output size limit")):
    path = sparse_zip(name, size)
    limits = (None,) if asan else (None, 512 * 1024 * 1024)
    for limit_as in limits:
        code, output, elapsed = run(["conceal", cover, path], limit_as)
        if code == 0 or expected not in output:
            sys.exit(f"{name} (limit {limit_as}): expected {expected!r}, got rc={code}:\n{output}")
        if not asan and elapsed > 5:
            sys.exit(f"{name}: took {elapsed:.1f}s to refuse an impossible payload")
    os.remove(path)
    if glob.glob(os.path.join(work, "prdt_*.png")):
        sys.exit(f"{name}: left an image behind")
# A stored payload that fits: written as it is encrypted, so peak memory stays
# near the Argon2id floor (64 MiB) instead of growing with the ciphertext.
path = sparse_zip("fits.zip", 256 * 1024 * 1024)
process = subprocess.Popen([binary, "conceal", cover, path], cwd=work, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
output = process.stdout.read().decode(errors="replace")
_, status, usage = os.wait4(process.pid, 0)
if os.waitstatus_to_exitcode(status) != 0:
    sys.exit(f"fits.zip: conceal failed:\n{output}")
peak_mib = usage.ru_maxrss / 1024
if not asan and peak_mib > 160:
    sys.exit(f"fits.zip: conceal peaked at {peak_mib:.0f} MiB for a 256 MiB payload")
import re
image = re.search(r'PNG image: (\S+)', output).group(1)
key = re.search(r'\[\*\*\*(.*)\*\*\*\]', output).group(1)
recovered = os.path.join(work, "out"); os.mkdir(recovered)
os.rename(os.path.join(work, image), os.path.join(recovered, image))
os.remove(path); sparse_zip("expected.zip", 256 * 1024 * 1024)
result = subprocess.run([binary, "recover", image], cwd=recovered, input=(key + "\n").encode(),
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
if result.returncode != 0 or subprocess.run(["cmp", "-s", os.path.join(recovered, "fits.zip"),
                                             os.path.join(work, "expected.zip")]).returncode != 0:
    sys.exit(f"fits.zip: did not round-trip:\n{result.stdout.decode(errors='replace')}")
PY
    then
        echo "[FAIL] $case_id" >&2
        return 1
    fi
    rm -rf "$work"
    echo "[PASS] $case_id"
    return 0
}

# Recover reports its steps the way conceal does, per mode: Locating hidden
# data (and, for -r, the pixel carrier after the KEY), Deriving key,
# Decrypting, Decompressing (not for a stored payload) and Writing output file.
# A wrong KEY fails the step it is found at.
run_recover_step_output_case() {
    local case_id="recover_step_output"
    local work="$WORK_ROOT/$case_id"
    rm -rf "$work"
    mkdir -p "$work"
    cp "$TESTS/testdata/covers/cover.png" "$work/cover.png"
    cp "$TESTS/testdata/payloads/payload_text.txt" "$work/payload_text.txt"
    cp "$TESTS/testdata/payloads/payload_stored.gz" "$work/payload_stored.gz"
    pushd "$work" >/dev/null
    recover_step_fail() {
        popd >/dev/null
        echo "[FAIL] $case_id: $1" >&2
        [[ -n "${2:-}" ]] && cat "$work/$2" >&2
        return 1
    }
    # A well-formed KEY that belongs to none of the images below.
    "$BIN" conceal cover.png payload_text.txt > other.log 2>&1 || { recover_step_fail "conceal for the spare KEY failed" other.log; return 1; }
    local other_key
    other_key="$(extract_key other.log)"
    rm -f prdt_*.png

    local run mode payload
    for run in default:payload_text.txt -m:payload_text.txt -r:payload_text.txt default:payload_stored.gz; do
        mode="${run%%:*}"
        payload="${run#*:}"
        local name="${mode#-}_${payload%%.*}"
        local option=()
        [[ "$mode" != default ]] && option=("$mode")
        "$BIN" conceal "${option[@]}" cover.png "$payload" > "conceal_$name.log" 2>&1 ||
            { recover_step_fail "conceal $mode $payload failed" "conceal_$name.log"; return 1; }
        local key image
        key="$(extract_key "conceal_$name.log")"
        image="$(extract_embedded_image "conceal_$name.log")"
        mkdir -p "out_$name"
        mv "$image" "out_$name/in.png"
        local log="out_$name/recover.log"
        (cd "out_$name" && printf '%s\n' "$key" | "$BIN" recover in.png > recover.log 2>&1) ||
            { recover_step_fail "recover $mode $payload failed" "$log"; return 1; }

        local labels=("Locating hidden data...")
        local exact=(
            " ✓ Argon2id from the 128-bit recovery KEY."
            " ✓ XChaCha20-Poly1305, authenticated."
            "Complete! Please check your file."
        )
        local patterns=('^Image file: in\.png \([0-9.]+(KiB| bytes)\)\.$')
        case "$mode" in
            default) patterns+=('^ ✓ Default mode, IDAT chunk \([0-9]+(KiB| bytes)\)\.$') ;;
            -m) patterns+=('^ ✓ Mastodon mode, iCCP chunk \([0-9]+ bytes\)\.$') ;;
            -r)
                labels+=("Locating pixel carrier...")
                exact+=(" ✓ No IDAT or iCCP payload; the KEY is needed to check the pixels."
                    " ✓ Reddit mode, adaptive (1,15,4) matrix embedding in the least significant bits of keyed RGB samples.")
                ;;
        esac
        labels+=("Deriving key..." "Decrypting...")
        if [[ "$payload" == payload_text.txt ]]; then
            labels+=("Decompressing...")
            exact+=(" ✓ payload_text.txt (85 bytes).")
            patterns+=('^ ✓ [0-9]+ bytes -> 85 bytes\.$')
        else
            exact+=(" ✓ payload_stored.gz (48.8KiB).")
        fi
        labels+=("Writing output file...")

        local line
        for line in "${exact[@]}"; do
            grep -Fqx -- "$line" "$log" || { recover_step_fail "recover $mode $payload is missing: $line" "$log"; return 1; }
        done
        for line in "${patterns[@]}"; do
            grep -Eq -- "$line" "$log" || { recover_step_fail "recover $mode $payload has no line matching: $line" "$log"; return 1; }
        done
        if [[ "$(grep -E '^[A-Z][A-Za-z ]+\.\.\.$' "$log")" != "$(printf '%s\n' "${labels[@]}")" ]]; then
            recover_step_fail "recover $mode $payload steps are wrong or out of order" "$log"
            return 1
        fi
        if grep -Fq "Please wait" "$log"; then
            recover_step_fail "recover $mode $payload printed a wait note to a non-terminal"
            return 1
        fi

        # The wrong KEY fails at Decrypting, or for -r where the carrier is looked for.
        if [[ "$payload" == payload_text.txt && "$mode" != -m ]]; then
            local failed_step="Decrypting..." failed_result=" ✗ authentication failed."
            if [[ "$mode" == -r ]]; then
                failed_step="Locating pixel carrier..."
                failed_result=" ✗ no hidden data found for this KEY."
            fi
            log="out_$name/wrong_key.log"
            if (cd "out_$name" && printf '%s\n' "$other_key" | "$BIN" recover in.png > wrong_key.log 2>&1); then
                recover_step_fail "recover $mode accepted a wrong KEY" "$log"
                return 1
            fi
            if [[ "$(grep -Fx -A1 -- "$failed_step" "$log" | tail -n 1)" != "$failed_result" ]]; then
                recover_step_fail "recover $mode with a wrong KEY did not report: $failed_step$failed_result" "$log"
                return 1
            fi
        fi
    done
    popd >/dev/null
    echo "[PASS] $case_id"
    return 0
}

# Payloads above 64 MiB are deflated in parallel slices (pigz-style, joined by
# sync flushes); a compressible one must still round-trip byte for byte.
run_large_deflated_payload_case() {
    local case_id="large_deflated_payload"
    local work="$WORK_ROOT/$case_id"
    rm -rf "$work"
    mkdir -p "$work"
    cp "$TESTS/testdata/covers/cover.png" "$work/cover.png"
    python3 - "$work/large.txt" <<'PY'
import random, sys
rng = random.Random(11)
words = [bytes(rng.choice(b"abcdefghijklmnopqrstuvwxyz") for _ in range(rng.randint(2, 9))) for _ in range(4000)]
with open(sys.argv[1], "wb") as out:
    written = 0
    while written < 72 * 1024 * 1024:
        line = b" ".join(rng.choice(words) for _ in range(4000)) + b"\n"
        out.write(line)
        written += len(line)
PY
    pushd "$work" >/dev/null
    local image key
    if ! "$BIN" conceal cover.png large.txt > conceal.log 2>&1; then
        popd >/dev/null
        echo "[FAIL] $case_id: conceal failed" >&2
        cat "$work/conceal.log" >&2
        return 1
    fi
    image="$(extract_embedded_image conceal.log)"
    key="$(extract_key conceal.log)"
    mkdir out && cp "$image" out/in.png
    if ! (cd out && printf '%s\n' "$key" | "$BIN" recover in.png > recover.log 2>&1) ||
       ! cmp -s out/large.txt large.txt; then
        popd >/dev/null
        echo "[FAIL] $case_id: payload did not round-trip" >&2
        cat "$work/out/recover.log" >&2
        return 1
    fi
    popd >/dev/null
    echo "[PASS] $case_id"
    return 0
}

# Input files the user names are followed through symlinks (both builds), but
# what is opened must still be a regular file: a dangling link and a link to a
# FIFO are refused, and conceal leaves nothing behind for either.
run_input_symlinks_case() {
    local case_id="input_symlinks"
    local work="$WORK_ROOT/$case_id"
    rm -rf "$work"
    mkdir -p "$work/files"
    cp "$TESTS/testdata/covers/cover.png" "$work/files/cover.png"
    cp "$TESTS/testdata/payloads/payload_text.txt" "$work/files/payload.txt"
    pushd "$work" >/dev/null
    ln -s files/cover.png cover_link.png
    ln -s files/payload.txt payload_link.txt
    local image key
    if ! "$BIN" conceal cover_link.png payload_link.txt > conceal.log 2>&1; then
        popd >/dev/null
        echo "[FAIL] $case_id: conceal through symlinked inputs failed" >&2
        cat "$work/conceal.log" >&2
        return 1
    fi
    image="$(extract_embedded_image conceal.log)"
    key="$(extract_key conceal.log)"
    mkdir out && ln -s "../$image" out/image_link.png
    if ! (cd out && printf '%s\n' "$key" | "$BIN" recover image_link.png > recover.log 2>&1) ||
       ! cmp -s out/payload_link.txt files/payload.txt; then
        popd >/dev/null
        echo "[FAIL] $case_id: recover through a symlinked image failed" >&2
        cat "$work/out/recover.log" >&2
        return 1
    fi

    ln -s files/absent.png dangling.png
    mkfifo files/pipe.png
    ln -s files/pipe.png fifo_link.png
    local before after cover
    for cover in dangling.png fifo_link.png; do
        before="$(ls | sort)"
        if timeout 20 "$BIN" conceal "$cover" payload_link.txt > "$cover.log" 2>&1; then
            popd >/dev/null
            echo "[FAIL] $case_id: $cover was accepted as a cover" >&2
            return 1
        fi
        local expected
        if [[ "$cover" == dangling.png ]]; then
            expected='Error: Unable to open file "dangling.png": No such file or directory.'
        else
            expected='Error: File "fifo_link.png" not found or not a regular file.'
        fi
        if ! grep -Fqx -- "$expected" "$cover.log"; then
            popd >/dev/null
            echo "[FAIL] $case_id: $cover was refused without: $expected" >&2
            cat "$work/$cover.log" >&2
            return 1
        fi
        after="$(ls | grep -vx "$cover.log" | sort)"
        if [[ "$before" != "$after" ]]; then
            popd >/dev/null
            echo "[FAIL] $case_id: refusing $cover left output behind" >&2
            return 1
        fi
    done
    popd >/dev/null
    echo "[PASS] $case_id"
    return 0
}

# Default-mode recovery reads the payload from the file frame by frame. Its
# failures must still be clean: a bad payload-chunk CRC, a tampered ciphertext
# byte (CRC fixed up, so authentication catches it) and a file cut short each
# fail with the usual message and leave no output; a cover that carries its own
# iCCP profile still recovers.
run_streamed_recovery_case() {
    local case_id="streamed_recovery"
    local work="$WORK_ROOT/$case_id"
    rm -rf "$work"
    mkdir -p "$work"
    cp "$TESTS/testdata/payloads/payload_bin.bin" "$work/payload.bin"
    python3 - "$TESTS/testdata/covers/cover.png" "$work/cover_iccp.png" <<'PY'
import struct, sys, zlib
data = open(sys.argv[1], "rb").read()
def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
profile = b"\0" * 128 + b"acsp" + b"\0" * 124   # a placeholder ICC profile body
iccp = chunk(b"iCCP", b"test\0\0" + zlib.compress(profile))
open(sys.argv[2], "wb").write(data[:33] + iccp + data[33:])
PY
    pushd "$work" >/dev/null
    local cover image key
    for cover in cover_iccp.png; do
        if ! "$BIN" conceal "$cover" payload.bin > conceal.log 2>&1; then
            popd >/dev/null
            echo "[FAIL] $case_id: conceal with an iCCP cover failed" >&2
            cat "$work/conceal.log" >&2
            return 1
        fi
    done
    image="$(extract_embedded_image conceal.log)"
    key="$(extract_key conceal.log)"
    mkdir good && cp "$image" good/in.png
    if ! (cd good && printf '%s\n' "$key" | "$BIN" recover in.png > recover.log 2>&1) ||
       ! cmp -s good/payload.bin payload.bin; then
        popd >/dev/null
        echo "[FAIL] $case_id: a default image with a genuine iCCP did not recover" >&2
        return 1
    fi

    python3 - "$image" <<'PY'
import struct, sys, zlib
data = bytearray(open(sys.argv[1], "rb").read())
pos = 8
while True:
    length, kind = struct.unpack(">I4s", data[pos:pos + 8])
    if kind == b"IDAT" and data[pos + 8:pos + 11] == b"\x78\x5e\x5c":
        break
    pos += 12 + length
start, end = pos + 8, pos + 8 + length
bad_crc = bytearray(data); bad_crc[end] ^= 0xFF
open("bad_crc.png", "wb").write(bad_crc)
tampered = bytearray(data); tampered[start + 3 + 0x6E + 200] ^= 0x01
tampered[end:end + 4] = struct.pack(">I", zlib.crc32(tampered[pos + 4:end]) & 0xFFFFFFFF)
open("tampered.png", "wb").write(tampered)
open("truncated.png", "wb").write(data[:start + (end - start) // 2])
PY
    local name expected
    for name in bad_crc tampered truncated; do
        case "$name" in
            # Each build words the prefix its own way.
            bad_crc) expected="Corrupt PNG chunk CRC." ;;
            tampered) expected="File Recovery Error: Invalid recovery KEY or file is corrupt." ;;
            truncated) expected="" ;;
        esac
        mkdir "$name" && cp "$name.png" "$name/in.png"
        if (cd "$name" && printf '%s\n' "$key" | "$BIN" recover in.png > recover.log 2>&1); then
            popd >/dev/null
            echo "[FAIL] $case_id: $name image was recovered" >&2
            return 1
        fi
        if [[ -n "$expected" ]] && ! grep -Fq -- "$expected" "$name/recover.log"; then
            popd >/dev/null
            echo "[FAIL] $case_id: $name failed without: $expected" >&2
            cat "$work/$name/recover.log" >&2
            return 1
        fi
        if [[ "$(ls -A "$name" | grep -vx 'in.png' | grep -vx 'recover.log' | wc -l)" -ne 0 ]]; then
            popd >/dev/null
            echo "[FAIL] $case_id: $name left output behind" >&2
            ls -la "$work/$name" >&2
            return 1
        fi
    done
    popd >/dev/null
    echo "[PASS] $case_id"
    return 0
}

# The 20-unit filename limit counts UTF-8 bytes: nine "é" plus ".txt" is 13
# characters but 22 bytes, and the error must say bytes.
run_filename_byte_limit_case() {
    local case_id="filename_byte_limit"
    local work="$WORK_ROOT/$case_id"
    local name=$'\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9.txt'

    rm -rf "$work"
    mkdir -p "$work"
    cp "$TESTS/testdata/covers/cover.png" "$work/cover.png"
    printf 'hi\n' > "$work/$name"

    pushd "$work" >/dev/null
    if "$BIN" conceal cover.png "$name" > conceal.log 2>&1; then
        popd >/dev/null
        echo "[FAIL] $case_id: 22-byte filename was unexpectedly accepted" >&2
        return 1
    fi
    if ! grep -Fq "data filename must not exceed 20 bytes (UTF-8)" conceal.log ||
        grep -Fq "20 characters" conceal.log; then
        popd >/dev/null
        echo "[FAIL] $case_id: filename limit message does not describe bytes" >&2
        cat "$work/conceal.log" >&2
        return 1
    fi
    popd >/dev/null
    echo "[PASS] $case_id"
    return 0
}

run_case() {
    local case_id="$1" option="$2" payload_rel="$3"
    local cover_rel="${4:-testdata/covers/cover.png}"
    local cover="$TESTS/$cover_rel"
    local payload="$TESTS/$payload_rel"
    local work="$WORK_ROOT/$case_id"

    if [[ ! -f "$cover" ]]; then
        echo "[FAIL] $case_id: missing cover $cover_rel" >&2
        return 1
    fi
    if [[ ! -f "$payload" ]]; then
        echo "[FAIL] $case_id: missing payload $payload_rel" >&2
        return 1
    fi

    rm -rf "$work"
    mkdir -p "$work"
    cp "$cover" "$work/cover.png"
    cp "$payload" "$work/$(basename "$payload")"

    pushd "$work" >/dev/null
    if [[ -n "$option" ]]; then
        if ! "$BIN" conceal "$option" cover.png "$(basename "$payload")" > conceal.log 2>&1; then
            popd >/dev/null
            echo "[FAIL] $case_id: conceal command failed" >&2
            cat "$work/conceal.log" >&2
            return 1
        fi
    else
        if ! "$BIN" conceal cover.png "$(basename "$payload")" > conceal.log 2>&1; then
            popd >/dev/null
            echo "[FAIL] $case_id: conceal command failed" >&2
            cat "$work/conceal.log" >&2
            return 1
        fi
    fi

    local embedded
    local key
    embedded="$(extract_embedded_image conceal.log)"
    key="$(extract_key conceal.log)"
    if [[ -z "$embedded" || -z "$key" || ! -f "$embedded" ]]; then
        popd >/dev/null
        echo "[FAIL] $case_id: failed to parse conceal output" >&2
        cat "$work/conceal.log" >&2
        return 1
    fi

    if ! printf '%s\n' "$key" | "$BIN" recover "$embedded" > recover.log 2>&1; then
        popd >/dev/null
        echo "[FAIL] $case_id: recover command failed" >&2
        cat "$work/recover.log" >&2
        return 1
    fi

    local recovered
    recovered="$(extract_recovered_file recover.log)"
    if [[ -z "$recovered" || ! -f "$recovered" ]]; then
        popd >/dev/null
        echo "[FAIL] $case_id: failed to parse recovered filename" >&2
        cat "$work/recover.log" >&2
        return 1
    fi

    if ! cmp -s "$recovered" "$payload"; then
        popd >/dev/null
        echo "[FAIL] $case_id: recovered bytes differ from source payload" >&2
        return 1
    fi

    popd >/dev/null
    echo "[PASS] $case_id"
    return 0
}

# The Reddit carrier permutes sample positions with a Feistel network over the
# smallest power-of-two domain containing the cover's RGB sample count. When
# that domain's bit length is odd the two Feistel halves are unequal, which is a
# separate code path -- and one a change of carrier scheme can move without
# touching the even case at all. cover.png is even (128*128*3 = 49152, 16 bits),
# so for a long time the suite exercised only half of that.
#
# The reddit_odd_domain case below closes the gap, and this guard keeps it
# closed: it fails if either fixture stops providing the parity its case was
# added for, rather than letting the coverage lapse silently.
png_uint32_at() {
    # IHDR width and height are the big-endian uint32s at offsets 16 and 20.
    od -An -tu1 -j "$2" -N4 -- "$1" |
        awk '{ print $1 * 16777216 + $2 * 65536 + $3 * 256 + $4 }'
}

carrier_domain_bits() {
    local span=$(( $1 - 1 )) bits=0
    while (( span > 0 )); do
        bits=$(( bits + 1 ))
        span=$(( span >> 1 ))
    done
    printf '%s' "$bits"
}

run_carrier_domain_fixture_case() {
    local case_id="carrier_domain_fixtures"
    local spec cover_rel want cover width height samples bits got
    local specs=(
        $'testdata/covers/cover.png\teven'
        $'testdata/covers/cover_odd_domain.png\todd'
    )

    for spec in "${specs[@]}"; do
        IFS=$'\t' read -r cover_rel want <<<"$spec"
        cover="$TESTS/$cover_rel"
        if [[ ! -f "$cover" ]]; then
            echo "[FAIL] $case_id: missing cover $cover_rel" >&2
            return 1
        fi
        width="$(png_uint32_at "$cover" 16)"
        height="$(png_uint32_at "$cover" 20)"
        samples=$(( width * height * 3 ))
        bits="$(carrier_domain_bits "$samples")"
        if (( bits % 2 == 0 )); then got=even; else got=odd; fi
        if [[ "$got" != "$want" ]]; then
            echo "[FAIL] $case_id: $cover_rel is ${width}x${height} => ${samples} samples," \
                 "a ${bits}-bit ($got) carrier domain, but this fixture must be $want." \
                 "Regenerate it at dimensions whose sample count has an $want bit length," \
                 "or the odd/even carrier coverage is lost." >&2
            return 1
        fi
    done

    echo "[PASS] $case_id"
    return 0
}

# case_id, conceal option ("." for none), payload, cover (blank = cover.png).
CASES=(
    $'default\t.\ttestdata/payloads/payload_text.txt\t'
    $'default_bin\t.\ttestdata/payloads/payload_bin.bin\t'
    $'mastodon\t-m\ttestdata/payloads/payload_mast.bin\t'
    $'reddit\t-r\ttestdata/payloads/payload_text.txt\t'
    $'reddit_odd_domain\t-r\ttestdata/payloads/payload_odd.bin\ttestdata/covers/cover_odd_domain.png'
)

WORK_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/pdvrdt-roundtrip-work.XXXXXX")"

if run_capsize_case; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

if run_carrier_domain_fixture_case; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

if run_reddit_capacity_failure_case; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

if run_compression_header_case; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

if run_reddit_cover_types_case; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

if run_large_deflated_payload_case; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

if run_input_symlinks_case; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

if run_streamed_recovery_case; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

if run_filename_byte_limit_case; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

if run_conceal_step_output_case; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

if run_recover_step_output_case; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

if run_reddit_mastodon_pixel_limit_case; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

if run_mastodon_icc_profile_case; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

if run_broken_stdout_after_publish_case; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

if run_large_stored_payload_case; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

for row in "${CASES[@]}"; do
    IFS=$'\t' read -r case_id option payload_rel cover_rel <<<"$row"
    [[ "$option" == "." ]] && option=""
    [[ -n "$cover_rel" ]] || cover_rel="testdata/covers/cover.png"
    if run_case "$case_id" "$option" "$payload_rel" "$cover_rel"; then
        PASS=$((PASS + 1))
    else
        FAIL=$((FAIL + 1))
    fi
done

echo
echo "Round-trip test summary: PASS=$PASS FAIL=$FAIL"
echo "Binary: $BIN"

if [[ "$FAIL" -ne 0 ]]; then
    exit 1
fi
