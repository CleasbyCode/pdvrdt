#!/bin/bash
# Build pdvrdt and regenerate the committed golden embedded-PNG fixtures and
# manifest. Re-run after an intentional format or crypto-layout change, then
# review the diffs to golden/*/embedded.png and golden/manifest.tsv before
# committing.
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
STAGE_ROOT=""
CURRENT_WORK=""
BACKUP_GOLDEN=""
GOLDEN_COMMITTED=0

cleanup() {
    local status=$?

    if [[ -n "$BACKUP_GOLDEN" && -e "$BACKUP_GOLDEN" ]]; then
        if [[ "$GOLDEN_COMMITTED" -eq 1 ]]; then
            rm -rf -- "$BACKUP_GOLDEN"
        elif [[ ! -e "$GOLDEN" ]]; then
            if ! mv -T -- "$BACKUP_GOLDEN" "$GOLDEN"; then
                echo "Failed to restore golden fixtures from: $BACKUP_GOLDEN" >&2
                status=1
            fi
        fi
    fi

    [[ -z "$CURRENT_WORK" ]] || rm -rf -- "$CURRENT_WORK"
    [[ -z "$STAGE_ROOT" ]] || rm -rf -- "$STAGE_ROOT"
    [[ -z "$BUILD_DIR" ]] || rm -rf -- "$BUILD_DIR"
    trap - EXIT
    exit "$status"
}
trap cleanup EXIT

usage() {
    cat <<'EOF'
Usage: tests/generate_golden.sh [options]

Options:
  --no-build    Reuse PDVRDT_BIN, or the existing src/pdvrdt binary.
  -h, --help    Show this help.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build) NO_BUILD=1; shift;;
        -h|--help) usage; exit 0;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2;;
    esac
done

if [[ -n "$BIN" && "$BIN" != /* ]]; then
    BIN="$CALLER_PWD/${BIN#./}"
fi

bash "$TESTS/create_testdata.sh"

if [[ "$NO_BUILD" -eq 0 ]]; then
    BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/pdvrdt-golden-generate-build.XXXXXX")"
    BIN="$BUILD_DIR/pdvrdt"
    (cd "$ROOT" && PDVRDT_OUTPUT="$BIN" bash ./compile_pdvrdt.sh)
elif [[ -z "$BIN" ]]; then
    BIN="$ROOT/pdvrdt"
fi
if [[ ! -x "$BIN" ]]; then
    echo "Binary not found: $BIN" >&2
    exit 1
fi

extract_embedded_image() {
    sed -n 's/.*Saved "file-embedded" PNG image: \([^ ]*\) (.*/\1/p' "$1" | tail -n 1
}
extract_pin() {
    sed -n 's/.*Recovery PIN: \[\*\*\*\([0-9][0-9]*\)\*\*\*\].*/\1/p' "$1" | tail -n 1
}

# case_id  option(.|-m|-r)  payload_rel  kind(default|mastodon|reddit)  cover_rel
# "." means the default (no) conceal option; a blank cover means cover.png.
#
# The two reddit cases differ only in their cover's carrier-domain parity, and
# that is the whole point of having both. The Reddit carrier derives every
# sample position from the recovery PIN through a Feistel permutation, so a
# fixed image plus its PIN pins that permutation exactly -- something a
# conceal/recover round trip cannot do, because both sides move together. An
# even-bit-length domain uses equal Feistel halves and an odd one does not, so
# one case per parity is the minimum that detects a change to either.
CASES=(
    $'default_text\t.\ttestdata/payloads/payload_text.txt\tdefault\t'
    $'default_bin\t.\ttestdata/payloads/payload_bin.bin\tdefault\t'
    $'default_stored\t.\ttestdata/payloads/payload_stored.gz\tdefault\t'
    $'mastodon_text\t-m\ttestdata/payloads/payload_text.txt\tmastodon\t'
    $'mastodon_bin\t-m\ttestdata/payloads/payload_mast.bin\tmastodon\t'
    $'reddit_even_domain\t-r\ttestdata/payloads/payload_text.txt\treddit\t'
    $'reddit_odd_domain\t-r\ttestdata/payloads/payload_odd.bin\treddit\ttestdata/covers/cover_odd_domain.png'
)

# Recover-only fixtures for an embed layout no current conceal option produces:
# the payload IDAT preceded by a 512 KiB all-zero IDAT, as emitted by the
# retired platform-padding option. They cannot be regenerated, so they are
# carried forward verbatim from the committed tree — images already published
# in that layout must keep recovering. Delete these entries (and the matching
# golden/ directories) only if that compatibility is being dropped on purpose.
LEGACY_CASES=(
    legacy_padded_text
    legacy_padded_bin
)

COVER_REL="testdata/covers/cover.png"

STAGE_ROOT="$(mktemp -d "$TESTS/.golden-stage.XXXXXX")"
STAGED_GOLDEN="$STAGE_ROOT/golden"
STAGED_MANIFEST="$STAGED_GOLDEN/manifest.tsv"
mkdir -p "$STAGED_GOLDEN"

{
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        case_id option payload_rel golden_rel pin kind
    for row in "${CASES[@]}"; do
        IFS=$'\t' read -r case_id option payload_rel kind cover_rel <<<"$row"
        opt="$option"
        [[ "$opt" == "." ]] && opt=""
        [[ -n "$cover_rel" ]] || cover_rel="$COVER_REL"

        cover="$TESTS/$cover_rel"
        payload="$TESTS/$payload_rel"
        if [[ ! -f "$cover" || ! -f "$payload" ]]; then
            echo "Missing fixture for $case_id" >&2
            exit 1
        fi

        mkdir -p "$STAGED_GOLDEN/$case_id"
        CURRENT_WORK="$(mktemp -d "${TMPDIR:-/tmp}/pdvrdt-golden-case.XXXXXX")"
        pushd "$CURRENT_WORK" >/dev/null
        cp "$cover" cover.png
        cp "$payload" "$(basename "$payload")"
        if [[ -n "$opt" ]]; then
            if ! "$BIN" conceal "$opt" cover.png "$(basename "$payload")" > conceal.log 2>&1; then
                echo "Conceal failed for $case_id:" >&2
                cat conceal.log >&2
                popd >/dev/null
                exit 1
            fi
        else
            if ! "$BIN" conceal cover.png "$(basename "$payload")" > conceal.log 2>&1; then
                echo "Conceal failed for $case_id:" >&2
                cat conceal.log >&2
                popd >/dev/null
                exit 1
            fi
        fi
        embedded="$(extract_embedded_image conceal.log)"
        pin="$(extract_pin conceal.log)"
        if [[ -z "$embedded" || -z "$pin" || ! -f "$embedded" ]]; then
            echo "Conceal failed for $case_id:" >&2
            cat conceal.log >&2
            popd >/dev/null
            exit 1
        fi
        popd >/dev/null

        golden_rel="golden/$case_id/embedded.png"
        cp "$CURRENT_WORK/$embedded" "$STAGED_GOLDEN/$case_id/embedded.png"
        rm -rf -- "$CURRENT_WORK"
        CURRENT_WORK=""

        printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$case_id" "$option" "$payload_rel" "$golden_rel" "$pin" "$kind"
    done

    for legacy_case in "${LEGACY_CASES[@]}"; do
        legacy_row="$(awk -F'\t' -v id="$legacy_case" '$1 == id { print; exit }' "$MANIFEST")"
        legacy_image="$GOLDEN/$legacy_case/embedded.png"
        if [[ -z "$legacy_row" || ! -f "$legacy_image" ]]; then
            echo "Missing preserved fixture for $legacy_case (expected $legacy_image and a manifest row)" >&2
            exit 1
        fi
        mkdir -p "$STAGED_GOLDEN/$legacy_case"
        cp "$legacy_image" "$STAGED_GOLDEN/$legacy_case/embedded.png"
        printf '%s\n' "$legacy_row"
    done
} > "$STAGED_MANIFEST"

# Keep the committed fixture tree untouched until every case and the complete
# manifest have been generated successfully. The backup is restored by the EXIT
# trap if publication fails after the old tree has been moved aside.
if [[ -e "$GOLDEN" ]]; then
    BACKUP_GOLDEN="$(mktemp -d "$TESTS/.golden-backup.XXXXXX")"
    rmdir -- "$BACKUP_GOLDEN"
    mv -T -- "$GOLDEN" "$BACKUP_GOLDEN"
fi

if ! mv -T -- "$STAGED_GOLDEN" "$GOLDEN"; then
    echo "Failed to publish newly generated golden fixtures." >&2
    if [[ -n "$BACKUP_GOLDEN" && ! -e "$GOLDEN" ]]; then
        mv -T -- "$BACKUP_GOLDEN" "$GOLDEN"
        BACKUP_GOLDEN=""
    fi
    exit 1
fi
GOLDEN_COMMITTED=1

if [[ -n "$BACKUP_GOLDEN" ]]; then
    rm -rf -- "$BACKUP_GOLDEN"
    BACKUP_GOLDEN=""
fi

echo "Golden fixtures written to: $GOLDEN"
echo "Manifest: $MANIFEST"
wc -l "$MANIFEST"
