#!/bin/bash
# Focused PNG optimization regressions: 16-bit fidelity, APNG rejection, and
# color-profile preservation through both exact-copy and palette paths.
set -euo pipefail

TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$TESTS/.." && pwd)"
BIN="${PDVRDT_BIN:-$ROOT/pdvrdt}"

if [[ $# -gt 0 ]]; then
    if [[ "$1" != "--bin" || $# -ne 2 ]]; then
        echo "Usage: $0 [--bin /path/to/pdvrdt]" >&2
        exit 2
    fi
    BIN="$2"
fi

if [[ "$BIN" != /* ]]; then
    BIN="$(pwd -P)/${BIN#./}"
fi
if [[ ! -x "$BIN" ]]; then
    echo "Binary not found or not executable: $BIN" >&2
    exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

BIN="$BIN" WORK="$WORK" python3 - <<'PY'
import binascii
import os
import struct
import subprocess
import zlib
from pathlib import Path

SIG = b"\x89PNG\r\n\x1a\n"
BIN = Path(os.environ["BIN"])
WORK = Path(os.environ["WORK"])


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", binascii.crc32(kind + data) & 0xFFFFFFFF)


def make_png(path, color_type, bit_depth, rows, metadata=()):
    ihdr = struct.pack(">IIBBBBB", 68, 68, bit_depth, color_type, 0, 0, 0)
    data = SIG + chunk(b"IHDR", ihdr)
    for kind, body in metadata:
        data += chunk(kind, body)
    data += chunk(b"IDAT", zlib.compress(b"".join(b"\0" + row for row in rows), 9))
    data += chunk(b"IEND", b"")
    path.write_bytes(data)


def parse_png(path):
    data = path.read_bytes()
    if not data.startswith(SIG):
        raise AssertionError(f"{path}: bad PNG signature")
    chunks = []
    pos = len(SIG)
    while pos < len(data):
        length = struct.unpack_from(">I", data, pos)[0]
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        stored_crc = struct.unpack_from(">I", data, pos + 8 + length)[0]
        if stored_crc != binascii.crc32(kind + body) & 0xFFFFFFFF:
            raise AssertionError(f"{path}: bad {kind!r} CRC")
        chunks.append((kind, body))
        pos += length + 12
        if kind == b"IEND":
            break
    return chunks


def conceal(case_name, cover, *options):
    case_dir = WORK / case_name
    case_dir.mkdir()
    result = subprocess.run(
        [str(BIN), "conceal", *options, str(cover), str(WORK / "p.txt")],
        cwd=case_dir,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(f"{case_name}: conceal failed\n{result.stdout}")
    outputs = list(case_dir.glob("prdt_*.png"))
    if len(outputs) != 1:
        raise AssertionError(f"{case_name}: expected one output, found {len(outputs)}\n{result.stdout}")
    return outputs[0]


WORK.joinpath("p.txt").write_text("image regression payload\n", encoding="ascii")

# Two distinct 16-bit samples deliberately have the same high bytes. The old
# RGBA8 statistics path collapsed these to one palette entry.
rows16 = []
for y in range(68):
    row = bytearray()
    for x in range(68):
        values = (0x1200, 0x3400, 0x5600) if (x + y) % 2 == 0 else (0x12FF, 0x34FF, 0x56FF)
        row += struct.pack(">HHH", *values)
    rows16.append(bytes(row))
rgb16 = WORK / "rgb16.png"
make_png(rgb16, 2, 16, rows16)
rgb16_out = conceal("rgb16", rgb16)
source_chunks = parse_png(rgb16)
output_chunks = parse_png(rgb16_out)
if output_chunks[0][1][8:10] != bytes((16, 2)):
    raise AssertionError("rgb16: IHDR was quantized instead of remaining RGB16")
if next(body for kind, body in output_chunks if kind == b"IDAT") != next(body for kind, body in source_chunks if kind == b"IDAT"):
    raise AssertionError("rgb16: original pixel IDAT changed")
print("[PASS] RGB16 pixels and IDAT remain unchanged")

# Canonical sRGB companion values plus sBIT must survive an RGB8-to-palette
# rewrite and remain before PLTE.
chrm = struct.pack(">8I", 31270, 32900, 64000, 33000, 30000, 60000, 15000, 6000)
rgb_metadata = ((b"cHRM", chrm), (b"gAMA", struct.pack(">I", 45455)), (b"sRGB", b"\0"), (b"sBIT", b"\x08\x08\x08"))
rows8 = []
for y in range(68):
    rows8.append(b"".join((b"\x11\x22\x33" if (x + y) % 2 == 0 else b"\xaa\xbb\xcc") for x in range(68)))
rgb8 = WORK / "rgb8_metadata.png"
make_png(rgb8, 2, 8, rows8, rgb_metadata)
rgb8_out = conceal("rgb8_metadata", rgb8)
rgb8_chunks = parse_png(rgb8_out)
if rgb8_chunks[0][1][8:10] != bytes((8, 3)):
    raise AssertionError("rgb8_metadata: expected palette optimization")
for expected_kind, expected_body in rgb_metadata:
    matches = [(i, body) for i, (kind, body) in enumerate(rgb8_chunks) if kind == expected_kind]
    if len(matches) != 1 or matches[0][1] != expected_body:
        raise AssertionError(f"rgb8_metadata: {expected_kind.decode()} was not preserved exactly once")
    if matches[0][0] > next(i for i, (kind, _) in enumerate(rgb8_chunks) if kind == b"PLTE"):
        raise AssertionError(f"rgb8_metadata: {expected_kind.decode()} is after PLTE")
print("[PASS] cHRM/gAMA/sRGB/sBIT survive palette optimization in valid order")

# Metadata that can coexist with Mastodon's payload iCCP remains present.
rgb_chrm_gama = WORK / "rgb8_chrm_gama.png"
chrm_gama = rgb_metadata[:2]
make_png(rgb_chrm_gama, 2, 8, rows8, chrm_gama)
mastodon_metadata_out = conceal("mastodon_chrm_gama", rgb_chrm_gama, "-m")
mastodon_metadata_chunks = parse_png(mastodon_metadata_out)
for expected_kind, expected_body in chrm_gama:
    if sum(kind == expected_kind and body == expected_body for kind, body in mastodon_metadata_chunks) != 1:
        raise AssertionError(f"mastodon_chrm_gama: {expected_kind.decode()} was not preserved")
if sum(kind == b"iCCP" for kind, _ in mastodon_metadata_chunks) != 1:
    raise AssertionError("mastodon_chrm_gama: expected exactly one payload iCCP")
print("[PASS] cHRM/gAMA metadata survive Mastodon embedding")

# A small RGB ICC header is sufficient for LodePNG's metadata pass-through.
icc = bytearray(128)
icc[0:4] = struct.pack(">I", len(icc))
icc[12:16] = b"mntr"
icc[16:20] = b"RGB "
icc[20:24] = b"XYZ "
icc[36:40] = b"acsp"
iccp_body = b"profile\0\0" + zlib.compress(bytes(icc), 9)
rgb_iccp = WORK / "rgb8_iccp.png"
make_png(rgb_iccp, 2, 8, rows8, ((b"iCCP", iccp_body),))
iccp_out = conceal("rgb8_iccp", rgb_iccp)
iccp_chunks = parse_png(iccp_out)
if iccp_chunks[0][1][8:10] != bytes((8, 3)):
    raise AssertionError("rgb8_iccp: expected palette optimization")
iccp_matches = [(i, body) for i, (kind, body) in enumerate(iccp_chunks) if kind == b"iCCP"]
if len(iccp_matches) != 1 or iccp_matches[0][1] != iccp_body:
    raise AssertionError("rgb8_iccp: iCCP was not preserved exactly")
if iccp_matches[0][0] > next(i for i, (kind, _) in enumerate(iccp_chunks) if kind == b"PLTE"):
    raise AssertionError("rgb8_iccp: iCCP is after PLTE")
print("[PASS] iCCP survives palette optimization in valid order")

# Mastodon stores its payload in the one iCCP slot permitted by PNG. Existing
# iCCP/sRGB declarations are accepted and replaced, while compatible metadata
# and the required low-colour truecolor-to-palette conversion remain intact.
for label, cover in (("srgb", rgb8), ("iccp", rgb_iccp)):
    output = conceal(f"mastodon_profile_replace_{label}", cover, "-m")
    output_chunks = parse_png(output)
    if output_chunks[0][1][8:10] != bytes((8, 3)):
        raise AssertionError(f"mastodon_profile_replace_{label}: expected palette optimization")
    payload_iccp = [body for kind, body in output_chunks if kind == b"iCCP"]
    if len(payload_iccp) != 1 or not payload_iccp[0].startswith(b"icc\0\0"):
        raise AssertionError(f"mastodon_profile_replace_{label}: expected exactly one payload iCCP")
    if any(kind == b"sRGB" for kind, _ in output_chunks):
        raise AssertionError(f"mastodon_profile_replace_{label}: conflicting sRGB was retained")
    if label == "srgb":
        for expected_kind, expected_body in (rgb_metadata[0], rgb_metadata[1], rgb_metadata[3]):
            if sum(kind == expected_kind and body == expected_body for kind, body in output_chunks) != 1:
                raise AssertionError(
                    f"mastodon_profile_replace_srgb: compatible {expected_kind.decode()} metadata was not preserved")
print("[PASS] Mastodon replaces iCCP/sRGB declarations without blocking palette optimization")

# An iCCP that carries an older pdvrdt Mastodon payload is not color metadata
# and must not leak into a newly concealed image.
mastodon_seed = conceal("mastodon_seed", rgb16, "-m")
reconcealed = conceal("reconcealed_mastodon_seed", mastodon_seed)
if any(kind == b"iCCP" for kind, _ in parse_png(reconcealed)):
    raise AssertionError("reconcealed_mastodon_seed: stale pdvrdt iCCP payload was retained")
print("[PASS] stale pdvrdt Mastodon iCCP payload is removed from reused covers")

# RGBA sBIT has four fields while palette sBIT has only three. Keep the required
# PNG-32-to-PNG-8 conversion, preserve the RGB significance fields, and let tRNS
# carry the palette alpha values.
rows_rgba = []
for y in range(68):
    rows_rgba.append(b"".join((b"\x11\x22\x33\x80" if (x + y) % 2 == 0 else b"\xaa\xbb\xcc\xff") for x in range(68)))
rgba_sbit = WORK / "rgba_sbit.png"
make_png(rgba_sbit, 6, 8, rows_rgba, ((b"sRGB", b"\0"), (b"sBIT", b"\x08\x08\x08\x08")))
rgba_out = conceal("rgba_sbit", rgba_sbit)
rgba_chunks = parse_png(rgba_out)
if rgba_chunks[0][1][8:10] != bytes((8, 3)):
    raise AssertionError("rgba_sbit: expected PNG-32 to PNG-8 palette conversion")
if sum(kind == b"sBIT" and body == b"\x08\x08\x08" for kind, body in rgba_chunks) != 1:
    raise AssertionError("rgba_sbit: palette-compatible RGB sBIT fields were not preserved")
trns = [body for kind, body in rgba_chunks if kind == b"tRNS"]
if len(trns) != 1 or b"\x80" not in trns[0]:
    raise AssertionError("rgba_sbit: palette transparency was not preserved in tRNS")
print("[PASS] low-colour PNG-32 with sBIT converts to valid PNG-8 with transparency")

rgba_mastodon_out = conceal("mastodon_rgba_srgb_sbit", rgba_sbit, "-m")
rgba_mastodon_chunks = parse_png(rgba_mastodon_out)
if rgba_mastodon_chunks[0][1][8:10] != bytes((8, 3)):
    raise AssertionError("mastodon_rgba_srgb_sbit: expected PNG-32 to PNG-8 palette conversion")
if any(kind == b"sRGB" for kind, _ in rgba_mastodon_chunks):
    raise AssertionError("mastodon_rgba_srgb_sbit: conflicting sRGB was retained")
if sum(kind == b"iCCP" and body.startswith(b"icc\0\0") for kind, body in rgba_mastodon_chunks) != 1:
    raise AssertionError("mastodon_rgba_srgb_sbit: expected exactly one payload iCCP")
if sum(kind == b"sBIT" and body == b"\x08\x08\x08" for kind, body in rgba_mastodon_chunks) != 1:
    raise AssertionError("mastodon_rgba_srgb_sbit: compatible sBIT fields were not preserved")
if not any(kind == b"tRNS" and b"\x80" in body for kind, body in rgba_mastodon_chunks):
    raise AssertionError("mastodon_rgba_srgb_sbit: palette transparency was not preserved")
print("[PASS] Mastodon accepts an sRGB PNG-32 and preserves its required PNG-8 conversion")

# Any APNG control/data chunk is rejected before an output file is created.
base = rgb8.read_bytes()
ihdr_end = len(SIG) + 12 + 13
apng_chunks = (
    (b"acTL", struct.pack(">II", 1, 0)),
    (b"fcTL", bytes(26)),
    (b"fdAT", bytes(4)),
)
for apng_kind, apng_body in apng_chunks:
    label = apng_kind.decode("ascii")
    apng = WORK / f"animated_{label}.png"
    apng.write_bytes(base[:ihdr_end] + chunk(apng_kind, apng_body) + base[ihdr_end:])
    case_dir = WORK / f"apng_{label}"
    case_dir.mkdir()
    result = subprocess.run(
        [str(BIN), "conceal", str(apng), str(WORK / "p.txt")],
        cwd=case_dir,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if result.returncode == 0 or "APNG covers are not supported" not in result.stdout:
        raise AssertionError(f"apng_{label}: expected explicit rejection\n{result.stdout}")
    if list(case_dir.glob("prdt_*.png")):
        raise AssertionError(f"apng_{label}: rejection left an output image")
print("[PASS] acTL/fcTL/fdAT APNG covers are rejected explicitly")
PY
