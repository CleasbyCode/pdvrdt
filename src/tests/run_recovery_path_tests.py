#!/usr/bin/env python3
"""Recovery filename collision regressions using pre-existing golden images."""

import argparse
import csv
import os
import stat
import subprocess
import tempfile
from pathlib import Path


TESTS = Path(__file__).resolve().parent
FIXTURES = ("default_text", "mastodon_text", "reddit_v2_even_domain")
SCENARIOS = (
    "missing", "dangling_base", "dangling_suffix", "valid_symlink",
    "symlink_loop", "regular_file", "directory",
)
SENTINEL = b"existing data must survive recovery\x00\xff"


def snapshot(path):
    """Inspect each entry without following links, including dangling/loop links."""
    info = path.lstat()
    metadata = (info.st_dev, info.st_ino, info.st_mode)
    if stat.S_ISLNK(info.st_mode):
        return metadata, os.readlink(path)
    if stat.S_ISDIR(info.st_mode):
        return metadata, {entry.name: snapshot(entry) for entry in path.iterdir()}
    if stat.S_ISREG(info.st_mode):
        return metadata, path.read_bytes()
    raise AssertionError(f"unexpected file type: {path}")


def prepare(work, filename, scenario):
    base = work / filename
    first = work / f"{base.stem}_1{base.suffix}"
    second = work / f"{base.stem}_2{base.suffix}"
    if scenario == "missing":
        return base
    if scenario == "dangling_base":
        base.symlink_to("absent-target")
    elif scenario == "dangling_suffix":
        base.write_bytes(SENTINEL)
        first.symlink_to("absent-target")
        return second
    elif scenario == "valid_symlink":
        (work / "existing-target").write_bytes(SENTINEL)
        base.symlink_to("existing-target")
    elif scenario == "symlink_loop":
        base.symlink_to(base.name)
    elif scenario == "regular_file":
        base.write_bytes(SENTINEL)
    elif scenario == "directory":
        base.mkdir()
        (base / "keep.bin").write_bytes(SENTINEL)
    else:
        raise AssertionError(f"unknown scenario: {scenario}")
    return first


def run_case(binary, fixture, scenario):
    payload = TESTS / fixture["payload_rel"]
    image = TESTS / fixture["golden_rel"]
    with tempfile.TemporaryDirectory(prefix="pdvrdt-recovery-path-") as temp:
        work = Path(temp)
        output = prepare(work, payload.name, scenario)
        before = {entry.name: snapshot(entry) for entry in work.iterdir()}
        result = subprocess.run(
            [str(binary), "recover", str(image)], cwd=work,
            input=fixture["pin"] + "\n", text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30,
        )
        log = result.stdout
        if any(marker in log for marker in (
                "AddressSanitizer", "LeakSanitizer", "UndefinedBehaviorSanitizer",
                "runtime error:")):
            raise AssertionError(f"sanitizer diagnostic:\n{log}")
        for name, original in before.items():
            if snapshot(work / name) != original:
                raise AssertionError(f"existing entry changed: {name}")
        new_names = {entry.name for entry in work.iterdir()} - before.keys()
        expected_names = {output.name} if result.returncode == 0 else set()
        if new_names != expected_names:
            raise AssertionError(f"unexpected output or staged leftovers: {new_names}")
        if result.returncode != 0:
            raise AssertionError(f"recovery exited {result.returncode}:\n{log}")
        info = output.lstat()
        if not stat.S_ISREG(info.st_mode) or stat.S_IMODE(info.st_mode) != 0o600:
            raise AssertionError("recovered output must be a regular file with mode 600")
        if output.read_bytes() != payload.read_bytes():
            raise AssertionError("recovered payload differs from golden bytes")
        if f"Extracted hidden file: {output.name} (" not in log:
            raise AssertionError(f"recovery did not report the expected filename:\n{log}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", default=os.environ.get("PDVRDT_BIN", TESTS.parent / "pdvrdt"),
                        type=Path, help="existing pdvrdt binary (default: ../pdvrdt)")
    args = parser.parse_args()
    binary = args.bin.resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error(f"binary is missing or not executable: {binary}")
    with (TESTS / "golden/manifest.tsv").open(newline="") as manifest:
        fixtures = {row["case_id"]: row for row in csv.DictReader(manifest, delimiter="\t")}
    passed = failed = 0
    for case_id in FIXTURES:
        for scenario in SCENARIOS:
            label = f"{case_id}/{scenario}"
            try:
                run_case(binary, fixtures[case_id], scenario)
            except (AssertionError, OSError, subprocess.TimeoutExpired) as error:
                print(f"[FAIL] {label}: {error}", flush=True)
                failed += 1
            else:
                print(f"[PASS] {label}", flush=True)
                passed += 1
    print(f"Recovery path checks: {passed} passed, {failed} failed.")
    return bool(failed)


if __name__ == "__main__":
    raise SystemExit(main())
