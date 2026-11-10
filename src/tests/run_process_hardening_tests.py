#!/usr/bin/env python3
"""Process hardening checks: core dumps off and memory unreadable to other
processes, a warning (and a normal run) when the system refuses either
setting, and a program name with terminal control bytes escaped in the usage
text.

--style cli      pdvrdt or pdvrdt-rs (default)
--style web-in   the web build's pdv_in
--style web-out  the web build's pdv_out
"""

import argparse
import csv
import glob
import os
import shutil
import subprocess
import tempfile
import time
from pathlib import Path


TESTS = Path(__file__).resolve().parent
COVER = TESTS / "testdata/covers/cover.png"
PAYLOAD = TESTS / "testdata/payloads/payload_text.txt"
FIXTURE = "key_default_text"

# Fails setrlimit(RLIMIT_CORE) and prctl(PR_SET_DUMPABLE) with EPERM when
# $REFUSE names them ("core", "dump"); every other call goes to the kernel.
REFUSAL_SHIM = r"""
#define _GNU_SOURCE
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
static int refused(const char* what) {
    const char* r = getenv("REFUSE");
    return r && strstr(r, what);
}
int setrlimit(__rlimit_resource_t res, const struct rlimit* lim) {
    if (res == RLIMIT_CORE && refused("core")) { errno = EPERM; return -1; }
    return (int)syscall(SYS_setrlimit, res, lim);
}
int prctl(int op, ...) {
    va_list ap;
    unsigned long a[4];
    va_start(ap, op);
    for (int i = 0; i < 4; ++i) a[i] = va_arg(ap, unsigned long);
    va_end(ap);
    if (op == PR_SET_DUMPABLE && refused("dump")) { errno = EPERM; return -1; }
    return (int)syscall(SYS_prctl, op, a[0], a[1], a[2], a[3]);
}
"""


class Skip(Exception):
    pass


def fixture_row():
    with (TESTS / "golden/manifest.tsv").open(newline="") as manifest:
        for row in csv.DictReader(manifest, delimiter="\t"):
            if row["case_id"] == FIXTURE:
                return row
    raise AssertionError(f"{FIXTURE} missing from the golden manifest")


def normal_run(style, work):
    """A command that succeeds and the glob of the file it writes."""
    if style == "cli":
        return ["conceal", str(COVER), str(PAYLOAD)], None, "*.png"
    if style == "web-in":
        return [str(COVER), str(PAYLOAD)], None, "*.png"
    row = fixture_row()
    (work / "key.txt").write_text(row["key"] + "\n")
    return [str(TESTS / row["golden_rel"]), "key.txt"], None, Path(row["payload_rel"]).name


def check_not_inspectable(binary, style):
    with tempfile.TemporaryDirectory(prefix="pdvrdt-hardening-") as temp:
        work = Path(temp)
        if style == "cli":
            # Waits at the KEY prompt for as long as stdin stays open.
            args = ["recover", str(TESTS / fixture_row()["golden_rel"])]
        elif style == "web-in":
            # The web build never waits for input: conceal a payload large
            # enough (random, so slow to compress) to inspect it mid-run.
            with open(work / "big.bin", "wb") as big:
                big.write(os.urandom(96 << 20))
            args = [str(COVER), "big.bin"]
        else:
            raise Skip("pdv_out never waits; its hardening is the same shared header as pdv_in")
        process = subprocess.Popen([str(binary), *args], cwd=work, stdin=subprocess.PIPE,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        proc = Path(f"/proc/{process.pid}")
        try:
            # The child is inspectable until it execs the binary, whose first
            # act is to harden itself, so wait for the fd list to close.
            deadline = time.monotonic() + 5
            while True:
                if process.poll() is not None:
                    raise AssertionError("process exited before it could be inspected")
                try:
                    os.listdir(proc / "fd")
                except PermissionError:
                    break
                if time.monotonic() > deadline:
                    raise AssertionError("a same-user process could still list /proc/PID/fd")
                time.sleep(0.005)
            try:
                os.close(os.open(proc / "mem", os.O_RDONLY))
            except PermissionError:
                pass
            else:
                raise AssertionError("a same-user process could open /proc/PID/mem")
            limits = (proc / "limits").read_text()
            core = next(line for line in limits.splitlines() if line.startswith("Max core file size"))
            if core.split()[4:6] != ["0", "0"]:
                raise AssertionError(f"core dumps are not disabled: {core}")
        finally:
            process.kill()
            process.wait()


def check_refusal_warns(binary, style):
    if shutil.which("cc") is None:
        raise Skip("no C compiler for the refusal shim")
    if "libasan" in subprocess.run(["ldd", str(binary)], capture_output=True, text=True).stdout:
        raise Skip("ASan builds refuse a preloaded library ahead of their runtime")
    with tempfile.TemporaryDirectory(prefix="pdvrdt-refusal-") as temp:
        root = Path(temp)
        (root / "refuse.c").write_text(REFUSAL_SHIM)
        shim = root / "refuse.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-O2", "-o", str(shim), str(root / "refuse.c")],
                       check=True)
        for refuse, expect in (("core,dump", "RLIMIT_CORE 0 and PR_SET_DUMPABLE 0"),
                               ("core", "RLIMIT_CORE 0,"), ("dump", "PR_SET_DUMPABLE 0,"),
                               ("none", None)):
            work = root / refuse.replace(",", "_")
            work.mkdir()
            args, stdin, output_glob = normal_run(style, work)
            env = dict(os.environ, REFUSE=refuse, LD_PRELOAD=str(shim))
            result = subprocess.run([str(binary), *args], cwd=work, env=env, input=stdin,
                                    capture_output=True, text=True, timeout=60)
            if result.returncode != 0:
                raise AssertionError(f"[{refuse}] command failed:\n{result.stdout}{result.stderr}")
            if not glob.glob(str(work / output_glob)):
                raise AssertionError(f"[{refuse}] no {output_glob} written")
            if expect is None:
                if "Warning" in result.stderr:
                    raise AssertionError(f"[none] warned although nothing was refused:\n{result.stderr}")
            elif (f"Warning: The system refused {expect}" not in result.stderr
                  or "Continuing anyway." not in result.stderr):
                raise AssertionError(f"[{refuse}] missing or wrong warning (wanted {expect}):\n"
                                     f"{result.stderr}")


def check_program_name_escaped(binary, style):
    with tempfile.TemporaryDirectory(prefix="pdvrdt-progname-") as temp:
        link = Path(temp) / "pd\x1b]0;owned\x07X"
        link.symlink_to(binary)
        result = subprocess.run([str(link)], capture_output=True, timeout=30)
        text = result.stdout + result.stderr
        if b"Usage: pd\\x1B]0;owned\\x07X" not in text:
            raise AssertionError(f"usage text does not show the escaped name:\n{text!r}")
        if b"\x1b" in text or b"\x07" in text:
            raise AssertionError(f"raw control bytes reached the output:\n{text!r}")


CHECKS = (
    ("process not inspectable / core dumps off", check_not_inspectable),
    ("refused hardening warns and carries on", check_refusal_warns),
    ("program name escaped in usage", check_program_name_escaped),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bin", default=os.environ.get("PDVRDT_BIN", TESTS.parent / "pdvrdt"),
                        type=Path, help="binary to test (default: ../pdvrdt)")
    parser.add_argument("--style", choices=("cli", "web-in", "web-out"), default="cli")
    args = parser.parse_args()
    binary = args.bin.resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error(f"binary is missing or not executable: {binary}")
    passed = failed = skipped = 0
    for label, check in CHECKS:
        try:
            check(binary, args.style)
        except Skip as reason:
            print(f"[SKIP] {label}: {reason}", flush=True)
            skipped += 1
        except (AssertionError, OSError, subprocess.SubprocessError) as error:
            print(f"[FAIL] {label}: {error}", flush=True)
            failed += 1
        else:
            print(f"[PASS] {label}", flush=True)
            passed += 1
    print(f"Process hardening checks: {passed} passed, {failed} failed, {skipped} skipped.")
    return bool(failed)


if __name__ == "__main__":
    raise SystemExit(main())
