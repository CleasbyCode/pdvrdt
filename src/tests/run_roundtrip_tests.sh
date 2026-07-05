#!/bin/bash
# Fresh conceal/recover round-trip regression tests for pdvrdt.
#
# These complement the golden recover fixtures by exercising the current
# conceal pipeline, parsing the generated recovery PIN/output image, recovering,
# and comparing the recovered payload bytes.
set -euo pipefail

TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$TESTS/.." && pwd)"
BIN="${PDVRDT_BIN:-$ROOT/pdvrdt}"
NO_BUILD=0

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

need_cmd cmp
need_cmd sed

if [[ "$BIN" != /* ]]; then
    BIN="$(pwd -P)/${BIN#./}"
fi

if [[ "$NO_BUILD" -eq 0 ]]; then
    (cd "$ROOT" && bash ./compile_pdvrdt.sh)
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

run_case() {
    local case_id="$1" option="$2" payload_rel="$3"
    local cover="$TESTS/testdata/covers/cover.png"
    local payload="$TESTS/$payload_rel"
    local work="$TESTS/.work_roundtrip/$case_id"

    if [[ ! -f "$cover" ]]; then
        echo "[FAIL] $case_id: missing cover testdata/covers/cover.png" >&2
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

CASES=(
    $'default\t.\ttestdata/payloads/payload_text.txt'
    $'mastodon\t-m\ttestdata/payloads/payload_mast.bin'
    $'reddit\t-r\ttestdata/payloads/payload_bin.bin'
)

mkdir -p "$TESTS/.work_roundtrip"
trap 'rm -rf "$TESTS/.work_roundtrip"' EXIT

for row in "${CASES[@]}"; do
    IFS=$'\t' read -r case_id option payload_rel <<<"$row"
    [[ "$option" == "." ]] && option=""
    if run_case "$case_id" "$option" "$payload_rel"; then
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
