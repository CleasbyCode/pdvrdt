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
BIN="${PDVRDT_BIN:-$ROOT/pdvrdt}"
NO_BUILD=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build) NO_BUILD=1; shift;;
        *) echo "Unknown option: $1" >&2; exit 2;;
    esac
done

bash "$TESTS/create_testdata.sh"

if [[ "$NO_BUILD" -eq 0 ]]; then
    (cd "$ROOT" && bash ./compile_pdvrdt.sh)
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

# case_id  option(.|-m|-r)  payload_rel  kind(default|mastodon|reddit)
# "." means the default (no) conceal option.
CASES=(
    $'default_text\t.\ttestdata/payloads/payload_text.txt\tdefault'
    $'default_bin\t.\ttestdata/payloads/payload_bin.bin\tdefault'
    $'default_stored\t.\ttestdata/payloads/payload_stored.gz\tdefault'
    $'mastodon_text\t-m\ttestdata/payloads/payload_text.txt\tmastodon'
    $'mastodon_bin\t-m\ttestdata/payloads/payload_mast.bin\tmastodon'
    $'reddit_text\t-r\ttestdata/payloads/payload_text.txt\treddit'
    $'reddit_bin\t-r\ttestdata/payloads/payload_bin.bin\treddit'
)

COVER_REL="testdata/covers/cover.png"

rm -rf "$GOLDEN"
mkdir -p "$GOLDEN"

{
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        case_id option payload_rel golden_rel pin kind
    for row in "${CASES[@]}"; do
        IFS=$'\t' read -r case_id option payload_rel kind <<<"$row"
        opt="$option"
        [[ "$opt" == "." ]] && opt=""

        cover="$TESTS/$COVER_REL"
        payload="$TESTS/$payload_rel"
        if [[ ! -f "$cover" || ! -f "$payload" ]]; then
            echo "Missing fixture for $case_id" >&2
            exit 1
        fi

        mkdir -p "$GOLDEN/$case_id"
        work="$(mktemp -d)"
        pushd "$work" >/dev/null
        cp "$cover" cover.png
        cp "$payload" "$(basename "$payload")"
        if [[ -n "$opt" ]]; then
            "$BIN" conceal "$opt" cover.png "$(basename "$payload")" > conceal.log 2>&1
        else
            "$BIN" conceal cover.png "$(basename "$payload")" > conceal.log 2>&1
        fi
        embedded="$(extract_embedded_image conceal.log)"
        pin="$(extract_pin conceal.log)"
        if [[ -z "$embedded" || -z "$pin" || ! -f "$embedded" ]]; then
            echo "Conceal failed for $case_id:" >&2
            cat conceal.log >&2
            popd >/dev/null
            rm -rf "$work"
            exit 1
        fi
        popd >/dev/null

        golden_rel="golden/$case_id/embedded.png"
        cp "$work/$embedded" "$TESTS/$golden_rel"
        rm -rf "$work"

        printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$case_id" "$option" "$payload_rel" "$golden_rel" "$pin" "$kind"
    done
} > "$MANIFEST"

echo "Golden fixtures written to: $GOLDEN"
echo "Manifest: $MANIFEST"
wc -l "$MANIFEST"
