#!/bin/bash
# Fresh conceal/recover round-trip regression tests for pdvrdt.
#
# These complement the golden recover fixtures by exercising the current
# conceal pipeline, parsing the generated recovery PIN/output image, recovering,
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

extract_pin() {
    sed -n 's/.*Recovery PIN: \[\*\*\*\([0-9][0-9]*\)\*\*\*\].*/\1/p' "$1" | tail -n 1
}

extract_recovered_file() {
    sed -n 's/.*Extracted hidden file: \([^ ]*\) (.*/\1/p' "$1" | tail -n 1
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

	local required_lines=(
		"Reddit capacity check for conceal -r mode only."
		"Cover Image: 0KiB, 128x128, Non-interlaced 8-bit RGB PNG, Adaptive (1,15,4) matrix embedding."
		"Theoretical adaptive capacity limit for this cover image:              1619 bytes (~1KiB)."
		"Conservative maximum compressed capacity with a 20-character filename: 1401 bytes (~1KiB)."
		"Recommended  maximum compressed capacity with a 20-character filename: 377 bytes (~0KiB)."
        "The final embedded PNG size can differ from the cover image and must remain within 20MB."
    )
    local expected
    for expected in "${required_lines[@]}"; do
        if ! grep -Fqx -- "$expected" capsize.log; then
            popd >/dev/null
            echo "[FAIL] $case_id: missing output line: $expected" >&2
            cat "$work/capsize.log" >&2
            return 1
        fi
    done
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
		"Cover Image: 0KiB, 128x128, Non-interlaced 8-bit RGB PNG, Adaptive (1,15,4) matrix embedding."
		"Compressed data file (payload) size: 50011 bytes (48KiB)."
		"Theoretical adaptive capacity limit for this cover image:              1619 bytes (~1KiB)."
		"Conservative maximum compressed capacity with a 20-character filename: 1401 bytes (~1KiB)."
		"Recommended  maximum compressed capacity with a 20-character filename: 377 bytes (~0KiB)."
		"Data File Size Error: "
		# The limit quoted must be the one actually enforced -- the maximum for
		# *this* payload's 17-character filename (1404), not the 20-character
		# conservative figure (1401) and not the recommendation 1024 below it.
		"Compressed payload size of 50011 bytes (48KiB) exceeds this cover image's maximum of 1404 bytes (~1KiB) for a 17-character filename."
		"Where capacity permits, leave at least 1KiB of headroom below that (377 bytes)."
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
    local pin
    embedded="$(extract_embedded_image conceal.log)"
    pin="$(extract_pin conceal.log)"
    if [[ -z "$embedded" || -z "$pin" || ! -f "$embedded" ]]; then
        popd >/dev/null
        echo "[FAIL] $case_id: failed to parse conceal output" >&2
        cat "$work/conceal.log" >&2
        return 1
    fi

    if ! printf '%s\n' "$pin" | "$BIN" recover "$embedded" > recover.log 2>&1; then
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
