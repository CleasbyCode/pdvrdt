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
BIN="${PDVRDT_BIN:-$ROOT/pdvrdt}"
NO_BUILD=0

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
        --bin) BIN="$2"; NO_BUILD=1; shift 2;;
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

if [[ "$NO_BUILD" -eq 0 ]]; then
    (cd "$ROOT" && bash ./compile_pdvrdt.sh)
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

PASS=0
FAIL=0

run_case() {
    local case_id="$1" option="$2" payload_rel="$3" golden_rel="$4" pin="$5" kind="$6"
    local payload="$TESTS/$payload_rel"
    local golden="$TESTS/$golden_rel"
    local work="$TESTS/.work/$case_id"

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

mkdir -p "$TESTS/.work"
trap 'rm -rf "$TESTS/.work"' EXIT

while IFS=$'\t' read -r case_id option payload_rel golden_rel pin kind; do
    [[ -z "$case_id" || "$case_id" == "case_id" ]] && continue
    [[ "$option" == "." ]] && option=""
    if run_case "$case_id" "$option" "$payload_rel" "$golden_rel" "$pin" "$kind"; then
        PASS=$((PASS + 1))
    else
        FAIL=$((FAIL + 1))
    fi
done < "$MANIFEST"

echo
echo "Golden test summary: PASS=$PASS FAIL=$FAIL"
echo "Binary: $BIN"

if [[ "$FAIL" -ne 0 ]]; then
    exit 1
fi
