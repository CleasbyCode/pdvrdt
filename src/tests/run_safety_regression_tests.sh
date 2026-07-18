#!/bin/bash
# Focused safety regressions for transactional reporting, PNG candidate limits,
# small valid covers, and representation-based platform budgets.
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
if ! command -v python3 >/dev/null 2>&1; then
    echo "Missing required command: python3" >&2
    exit 1
fi
if ! command -v "${CXX:-g++}" >/dev/null 2>&1; then
    echo "Missing required C++ compiler: ${CXX:-g++}" >&2
    exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf -- "$WORK"' EXIT

"${CXX:-g++}" -std=c++23 -shared -fPIC "$TESTS/close_eintr_shim.cpp" -ldl -o "$WORK/close_eintr_shim.so"
"${CXX:-g++}" -std=c++23 -O2 -I"$ROOT" \
    "$TESTS/input_snapshot_test.cpp" "$ROOT/compression.cpp" "$ROOT/io_utils.cpp" \
    -lsodium -lz -ldeflate -o "$WORK/input_snapshot_test"
"$WORK/input_snapshot_test" "$WORK/input_snapshot"

BIN="$BIN" WORK="$WORK" CLOSE_EINTR_SHIM="$WORK/close_eintr_shim.so" python3 - <<'PY'
import binascii
import filecmp
import os
import re
import struct
import subprocess
import zlib
from pathlib import Path

BIN = Path(os.environ["BIN"])
WORK = Path(os.environ["WORK"])
CLOSE_EINTR_SHIM = Path(os.environ["CLOSE_EINTR_SHIM"])
PNG_SIG = b"\x89PNG\r\n\x1a\n"
PIN_RE = re.compile(r"Recovery PIN: \[\*\*\*([0-9]+)\*\*\*\]")
IMAGE_RE = re.compile(r'Saved "file-embedded" PNG image: ([^ ]+) \(')


def chunk(kind, body):
    crc = binascii.crc32(kind + body) & 0xFFFFFFFF
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", crc)


def tiny_png(path):
    ihdr = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)
    scanline = b"\0\x12\x34\x56"
    path.write_bytes(PNG_SIG + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(scanline, 9)) + chunk(b"IEND", b""))


def conceal(case_dir, cover, payload, option=None, stdout=None, env=None):
    args = [str(BIN), "conceal"]
    if option:
        args.append(option)
    args.extend((str(cover), str(payload)))
    return subprocess.run(
        args,
        cwd=case_dir,
        text=stdout is None,
        stdout=subprocess.PIPE if stdout is None else stdout,
        stderr=subprocess.PIPE,
        env=env,
        check=False,
    )


def parse_conceal(result, case_dir):
    output = result.stdout
    pin_match = PIN_RE.search(output)
    image_match = IMAGE_RE.search(output)
    if result.returncode != 0 or not pin_match or not image_match:
        raise AssertionError(f"conceal failed or output was incomplete\n{output}\n{result.stderr}")
    image = case_dir / image_match.group(1)
    if not image.is_file():
        raise AssertionError(f"missing concealed image: {image}")
    return image, pin_match.group(1)


payload = WORK / "p.txt"
payload.write_text("safety regression payload\n", encoding="ascii")

# Validity is structural, not an arbitrary minimum file size.
tiny = WORK / "tiny.png"
tiny_png(tiny)
if tiny.stat().st_size >= 87:
    raise AssertionError("tiny fixture no longer exercises the removed 87-byte threshold")
tiny_case = WORK / "tiny_case"
tiny_case.mkdir()
tiny_result = conceal(tiny_case, tiny, payload)
parse_conceal(tiny_result, tiny_case)
print(f"[PASS] structurally valid {tiny.stat().st_size}-byte PNG cover is accepted")

# A failed PIN report must roll back the otherwise-complete output file.
full_case = WORK / "stdout_full"
full_case.mkdir()
with open("/dev/full", "wb", buffering=0) as full:
    full_result = conceal(full_case, tiny, payload, stdout=full)
if full_result.returncode == 0 or list(full_case.glob("prdt_*.png")):
    raise AssertionError("stdout failure did not fail and roll back the embedded image")
if b"reliably report" not in full_result.stderr:
    raise AssertionError(f"stdout failure produced the wrong diagnostic: {full_result.stderr!r}")
print("[PASS] failed filename/PIN reporting rolls back the output image")

# Linux closes the descriptor even when close reports EINTR; retrying is unsafe,
# but the error must still abort publication and remove the named output.
eintr_case = WORK / "close_eintr"
eintr_case.mkdir()
eintr_env = os.environ.copy()
eintr_env["LD_PRELOAD"] = str(CLOSE_EINTR_SHIM)
eintr_result = conceal(eintr_case, tiny, payload, env=eintr_env)
if eintr_result.returncode == 0 or list(eintr_case.glob("prdt_*.png")):
    raise AssertionError("close(EINTR) did not fail and roll back the embedded image")
if "Interrupted system call" not in eintr_result.stderr:
    raise AssertionError(f"close(EINTR) produced the wrong diagnostic: {eintr_result.stderr}")
print("[PASS] close(EINTR) is reported without retry and rolls back the output image")

# PNG permits one iCCP. Reject the second before another bounded inflation.
base = tiny.read_bytes()
ihdr_end = len(PNG_SIG) + 12 + 13
fake_iccp = b"icc\0\0" + zlib.compress(bytes(1024), 9)
duplicate = WORK / "duplicate_iccp.png"
duplicate.write_bytes(base[:ihdr_end] + chunk(b"iCCP", fake_iccp) + chunk(b"iCCP", fake_iccp) + base[ihdr_end:])
duplicate_result = subprocess.run(
    [str(BIN), "recover", str(duplicate)],
    stdin=subprocess.DEVNULL,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    check=False,
)
if duplicate_result.returncode == 0 or "Duplicate iCCP chunk" not in duplicate_result.stdout:
    raise AssertionError(f"duplicate iCCP was not rejected before PIN entry\n{duplicate_result.stdout}")
print("[PASS] duplicate iCCP candidates are rejected")

# Original size is deliberately above both platform limits. Repetitive bytes
# must still succeed because the post-compression representation fits.
large = WORK / "large.bin"
with large.open("wb") as stream:
    stream.truncate(22 * 1024 * 1024)

for label, option, limit in (("mastodon", "-m", 16 * 1024 * 1024), ("reddit", "-r", 20 * 1024 * 1024)):
    case_dir = WORK / f"compressible_{label}"
    case_dir.mkdir()
    result = conceal(case_dir, tiny, large, option)
    image, pin = parse_conceal(result, case_dir)
    if image.stat().st_size > limit:
        raise AssertionError(f"{label}: compressed output exceeds platform budget")
    recovered = subprocess.run(
        [str(BIN), "recover", str(image)],
        cwd=case_dir,
        input=pin + "\n",
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    recovered_path = case_dir / "large.bin"
    if recovered.returncode != 0 or not recovered_path.is_file() or not filecmp.cmp(recovered_path, large, shallow=False):
        raise AssertionError(f"{label}: large compressible round trip failed\n{recovered.stdout}")
    print(f"[PASS] {label} accepts and recovers an input larger than its post-compression limit")

# Conversely, incompressible data that cannot fit must fail without publishing
# an image. One payload is reused for both modes.
random_payload = WORK / "random.bin"
random_payload.write_bytes(os.urandom(22 * 1024 * 1024))
for label, option in (("mastodon", "-m"), ("reddit", "-r")):
    case_dir = WORK / f"oversize_{label}"
    case_dir.mkdir()
    result = conceal(case_dir, tiny, random_payload, option)
    if result.returncode == 0 or list(case_dir.glob("prdt_*.png")):
        raise AssertionError(f"{label}: oversized representation was published")
    if "File Size Error" not in result.stderr:
        raise AssertionError(f"{label}: missing size diagnostic\n{result.stderr}")
    print(f"[PASS] {label} rejects an oversized compressed representation without output")
PY
