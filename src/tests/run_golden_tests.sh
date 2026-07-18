#!/bin/bash
# Recover-only golden-file regression tests for pdvrdt.
#
# Each golden/ case ships a pre-built embedded PNG, its recovery PIN, and the
# expected payload bytes. This catches regressions in recover, PNG chunk
# parsing, and the per-mode embed layouts (default IDAT / Mastodon iCCP /
# Reddit padding) without relying on fresh conceal RNG.
set -euo pipefail

TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$TESTS/.." && pwd)"
GOLDEN="$TESTS/golden"
MANIFEST="$GOLDEN/manifest.tsv"
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
Usage: tests/run_golden_tests.sh [options]

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

need_cmd python3
need_cmd cmp
need_cmd stat

if [[ -n "$BIN" && "$BIN" != /* ]]; then
    BIN="$CALLER_PWD/${BIN#./}"
fi

if [[ "$NO_BUILD" -eq 0 ]]; then
    BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/pdvrdt-golden-build.XXXXXX")"
    BIN="$BUILD_DIR/pdvrdt"
    (cd "$ROOT" && PDVRDT_OUTPUT="$BIN" bash ./compile_pdvrdt.sh)
elif [[ -z "$BIN" ]]; then
    BIN="$ROOT/pdvrdt"
fi
if [[ ! -x "$BIN" ]]; then
    echo "Binary not found or not executable: $BIN" >&2
    exit 1
fi
if [[ ! -f "$MANIFEST" ]]; then
    echo "Missing golden manifest: $MANIFEST" >&2
    echo "Generate fixtures with: bash tests/generate_golden.sh" >&2
    exit 1
fi

extract_recovered_file() {
    sed -n 's/.*Extracted hidden file: \([^ ]*\) (.*/\1/p' "$1" | tail -n 1
}

assert_owner_only_permissions() {
    local perms
    perms="$(stat -c '%a' "$1" 2>/dev/null || true)"
    if [[ "$perms" != "600" ]]; then
        echo "[FAIL] $2: expected owner-only permissions (600), got ${perms:-unknown}" >&2
        return 1
    fi
    return 0
}

# Walk the PNG chunks and assert the per-mode embed layout matches `kind`:
#   default  -> a pdvrdt IDAT profile chunk (zlib prefix 78 5E 5C + KDF2 magic +
#               PDVRDT_SIG at profile offset 101), and no iCCP/Reddit padding.
#   reddit   -> the pdvrdt IDAT profile chunk plus the 512 KiB all-zero IDAT pad.
#   mastodon -> a pdvrdt iCCP chunk ("icc\0\0" prefix) and no pdvrdt IDAT.
assert_png_structure() {
    local image="$1" kind="$2" tag="$3"
    if ! PDVRDT_IMG="$image" PDVRDT_KIND="$kind" python3 - <<'PY'
import os, sys
from pathlib import Path

data = Path(os.environ["PDVRDT_IMG"]).read_bytes()
kind = os.environ["PDVRDT_KIND"]

PNG_SIG   = bytes([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A])
IDAT_PFX  = bytes([0x78, 0x5E, 0x5C])
PDV_SIG   = bytes([0xC6, 0x50, 0x3C, 0xEA, 0x5E, 0x9D, 0xF9])
ICCP_PFX  = b"icc\x00\x00"
KDF2      = b"KDF2"
KDF_OFF   = 0x2D          # DEFAULT_OFFSETS.kdf_metadata
SIG_OFF   = 101           # DEFAULT_PDV_SIG_OFFSET
REDDIT_PAD = 0x80000

if data[:8] != PNG_SIG:
    sys.exit("bad PNG signature")

def chunks(d):
    pos = 8
    while pos + 12 <= len(d):
        ln = int.from_bytes(d[pos:pos + 4], "big")
        ct = d[pos + 4:pos + 8]
        if pos + 12 + ln > len(d):
            break
        yield ct, d[pos + 8:pos + 8 + ln], ln
        pos += 12 + ln

has_pdv_idat = has_reddit_pad = has_pdv_iccp = False
for ct, body, ln in chunks(data):
    if ct == b"IDAT" and body[:3] == IDAT_PFX:
        prof = body[3:]
        if len(prof) >= SIG_OFF + len(PDV_SIG) and \
           prof[KDF_OFF:KDF_OFF + 4] == KDF2 and \
           prof[SIG_OFF:SIG_OFF + len(PDV_SIG)] == PDV_SIG:
            has_pdv_idat = True
    if ct == b"IDAT" and ln == REDDIT_PAD and body == bytes(ln):
        has_reddit_pad = True
    if ct == b"iCCP" and body[:5] == ICCP_PFX:
        has_pdv_iccp = True

if kind in ("default", "reddit"):
    if not has_pdv_idat:
        sys.exit("missing pdvrdt IDAT profile chunk")
    if has_pdv_iccp:
        sys.exit("unexpected iCCP chunk")
if kind == "default" and has_reddit_pad:
    sys.exit("unexpected Reddit padding IDAT")
if kind == "reddit" and not has_reddit_pad:
    sys.exit("missing 512 KiB Reddit padding IDAT")
if kind == "mastodon":
    if not has_pdv_iccp:
        sys.exit("missing pdvrdt iCCP chunk")
    if has_pdv_idat:
        sys.exit("unexpected pdvrdt IDAT chunk")
PY
    then
        echo "[FAIL] $tag: PNG structure check failed" >&2
        return 1
    fi
    return 0
}

EXPECTED_CASE_IDS=(
    default_text
    default_bin
    default_stored
    mastodon_text
    mastodon_bin
    reddit_text
    reddit_bin
)
declare -A EXPECTED_CASES=()
declare -A SEEN_CASES=()
for expected_case in "${EXPECTED_CASE_IDS[@]}"; do
    EXPECTED_CASES["$expected_case"]=1
done

PASS=0
FAIL=0
CASE_COUNT=0
LINE_NUMBER=0

run_case() {
    local case_id="$1" option="$2" payload_rel="$3" golden_rel="$4" pin="$5" kind="$6"
    local payload="$TESTS/$payload_rel"
    local golden="$TESTS/$golden_rel"
    local work="$WORK_ROOT/$case_id"

    if [[ ! -f "$payload" ]]; then
        echo "[FAIL] $case_id: missing payload $payload_rel" >&2
        return 1
    fi
    if [[ ! -f "$golden" ]]; then
        echo "[FAIL] $case_id: missing golden image $golden_rel (run generate_golden.sh)" >&2
        return 1
    fi

    rm -rf "$work"
    mkdir -p "$work"
    cp "$golden" "$work/input.png"

    assert_png_structure "$golden" "$kind" "$case_id" || return 1

    pushd "$work" >/dev/null
    if ! printf '%s\n' "$pin" | "$BIN" recover input.png > recover.log 2>&1; then
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

    if ! assert_owner_only_permissions "$recovered" "$case_id"; then
        popd >/dev/null
        return 1
    fi

    if ! cmp -s "$recovered" "$payload"; then
        popd >/dev/null
        echo "[FAIL] $case_id: recovered bytes differ from golden payload" >&2
        return 1
    fi

    popd >/dev/null
    echo "[PASS] $case_id"
    return 0
}

WORK_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/pdvrdt-golden-work.XXXXXX")"

while IFS=$'\t' read -r case_id option payload_rel golden_rel pin kind; do
    LINE_NUMBER=$((LINE_NUMBER + 1))
    if [[ "$LINE_NUMBER" -eq 1 ]]; then
        if [[ "$case_id" != "case_id" || "$option" != "option" ||
              "$payload_rel" != "payload_rel" || "$golden_rel" != "golden_rel" ||
              "$pin" != "pin" || "$kind" != "kind" ]]; then
            echo "[FAIL] invalid golden manifest header" >&2
            FAIL=$((FAIL + 1))
        fi
        continue
    fi
    [[ -z "$case_id" ]] && continue
    CASE_COUNT=$((CASE_COUNT + 1))
    if [[ -z "${EXPECTED_CASES[$case_id]+present}" ]]; then
        echo "[FAIL] unexpected golden case in manifest: $case_id" >&2
        FAIL=$((FAIL + 1))
        continue
    fi
    if [[ -n "${SEEN_CASES[$case_id]+present}" ]]; then
        echo "[FAIL] duplicate golden case in manifest: $case_id" >&2
        FAIL=$((FAIL + 1))
        continue
    fi
    SEEN_CASES["$case_id"]=1
    if [[ -z "$payload_rel" || -z "$golden_rel" || -z "$pin" || -z "$kind" ]]; then
        echo "[FAIL] malformed manifest row for case: $case_id" >&2
        FAIL=$((FAIL + 1))
        continue
    fi
    [[ "$option" == "." ]] && option=""
    if run_case "$case_id" "$option" "$payload_rel" "$golden_rel" "$pin" "$kind"; then
        PASS=$((PASS + 1))
    else
        FAIL=$((FAIL + 1))
    fi
done < "$MANIFEST"

if [[ "$CASE_COUNT" -eq 0 ]]; then
    echo "[FAIL] golden manifest contains no test cases" >&2
    FAIL=$((FAIL + 1))
elif [[ "$CASE_COUNT" -ne "${#EXPECTED_CASE_IDS[@]}" ]]; then
    echo "[FAIL] expected ${#EXPECTED_CASE_IDS[@]} golden cases, found $CASE_COUNT" >&2
    FAIL=$((FAIL + 1))
fi

for expected_case in "${EXPECTED_CASE_IDS[@]}"; do
    if [[ -z "${SEEN_CASES[$expected_case]+present}" ]]; then
        echo "[FAIL] missing golden case from manifest: $expected_case" >&2
        FAIL=$((FAIL + 1))
    fi
done

echo
echo "Golden test summary: PASS=$PASS FAIL=$FAIL"
echo "Binary: $BIN"

if [[ "$FAIL" -ne 0 ]]; then
    exit 1
fi
