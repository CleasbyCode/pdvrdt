#!/usr/bin/env python3
"""PTY regressions for masked PIN entry and signal-safe terminal restoration."""

import argparse
import os
import pty
import select
import signal
import subprocess
import termios
import time
from pathlib import Path


def read_until(master_fd: int, needle: bytes, timeout: float) -> bytes:
    deadline = time.monotonic() + timeout
    output = bytearray()
    while needle not in output and time.monotonic() < deadline:
        ready, _, _ = select.select([master_fd], [], [], max(0.0, deadline - time.monotonic()))
        if not ready:
            break
        try:
            chunk = os.read(master_fd, 4096)
        except OSError:
            break
        if not chunk:
            break
        output.extend(chunk)
    return bytes(output)


def start_recovery(binary: Path, image: Path):
    master_fd, slave_fd = pty.openpty()
    original = termios.tcgetattr(slave_fd)
    process = subprocess.Popen(
        [str(binary), "recover", str(image)],
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        close_fds=True,
    )
    prompt = read_until(master_fd, b"PIN: ", 5.0)
    if b"PIN: " not in prompt:
        process.kill()
        process.wait()
        os.close(master_fd)
        os.close(slave_fd)
        raise AssertionError(f"PIN prompt not observed: {prompt!r}")
    return process, master_fd, slave_fd, original, prompt


def assert_restored(slave_fd: int, original, label: str):
    current = termios.tcgetattr(slave_fd)
    mask = termios.ECHO | termios.ICANON
    if current[3] & mask != original[3] & mask:
        raise AssertionError(f"{label}: ECHO/ICANON were not restored")


def test_signal(binary: Path, image: Path, signal_number: int):
    process, master_fd, slave_fd, original, _ = start_recovery(binary, image)
    try:
        active = termios.tcgetattr(slave_fd)
        if active[3] & (termios.ECHO | termios.ICANON):
            raise AssertionError(f"signal {signal_number}: secure PIN modes were not active")
        os.kill(process.pid, signal_number)

        if signal_number == signal.SIGTSTP:
            # Job-control stop signals can be discarded for an orphaned process
            # group (as in some CI harnesses). In either case, pdvrdt must restore
            # the terminal before the stop is acted upon or PIN entry aborts.
            time.sleep(0.1)
            assert_restored(slave_fd, original, "signal SIGTSTP")
            try:
                os.kill(process.pid, signal.SIGCONT)
            except ProcessLookupError:
                pass
            return_code = process.wait(timeout=5.0)
            if return_code == 0:
                raise AssertionError("SIGTSTP: PIN entry resumed instead of aborting safely")
            return

        return_code = process.wait(timeout=5.0)
        if return_code != -signal_number:
            raise AssertionError(
                f"signal {signal_number}: expected {-signal_number}, got {return_code}")
        assert_restored(slave_fd, original, f"signal {signal_number}")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master_fd)
        os.close(slave_fd)


def test_masking(binary: Path, image: Path):
    digits = b"987654321"
    process, master_fd, slave_fd, original, output = start_recovery(binary, image)
    try:
        os.write(master_fd, digits + b"\n")
        output += read_until(master_fd, b"Invalid PIN", 10.0)
        return_code = process.wait(timeout=10.0)
        # Drain messages emitted after the recovery error, including sanitizer
        # diagnostics at process exit. An empty needle would skip this read.
        output += read_until(master_fd, b"\x00", 0.1)
        if return_code == 0:
            raise AssertionError("wrong PIN unexpectedly succeeded")
        if any(marker in output for marker in
               (b"AddressSanitizer", b"LeakSanitizer", b"runtime error:")):
            raise AssertionError(f"sanitizer failure during PIN entry: {output!r}")
        if digits in output:
            raise AssertionError(f"terminal echoed the raw PIN: {output!r}")
        if b"*" * len(digits) not in output:
            raise AssertionError(f"masked PIN feedback was not observed: {output!r}")
        assert_restored(slave_fd, original, "wrong-PIN completion")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master_fd)
        os.close(slave_fd)


def test_broken_stdout(binary: Path, image: Path, stage: str):
    master_fd, slave_fd = pty.openpty()
    original = termios.tcgetattr(slave_fd)
    read_fd, write_fd = os.pipe()
    process = None
    try:
        if stage == "prompt":
            # The recovery prompt is the first stdout write. Close its reader
            # before exec so SIGPIPE is guaranteed at that write.
            os.close(read_fd)
            read_fd = -1
        process = subprocess.Popen(
            [str(binary), "recover", str(image)],
            stdin=slave_fd,
            stdout=write_fd,
            stderr=slave_fd,
            close_fds=True,
            restore_signals=True,
        )
        os.close(write_fd)
        write_fd = -1
        if stage != "prompt":
            output = read_until(read_fd, b"PIN: ", 5.0)
            if b"PIN: " not in output:
                raise AssertionError(f"broken stdout at {stage}: PIN prompt not observed")
            active = termios.tcgetattr(slave_fd)
            if active[3] & (termios.ECHO | termios.ICANON):
                raise AssertionError(f"broken stdout at {stage}: secure PIN modes were not active")
            if stage in ("backspace", "newline"):
                os.write(master_fd, b"9")
                masked = read_until(read_fd, b"*", 5.0)
                if b"*" not in masked or b"9" in masked:
                    raise AssertionError(f"broken stdout at {stage}: masked feedback not observed")
            # Observing the flushed prompt/mask synchronizes with the child.
            # Only after all readers are closed do we trigger its next write.
            os.close(read_fd)
            read_fd = -1
            os.write(master_fd, {"digit": b"9", "backspace": b"\x7f", "newline": b"\n"}[stage])

        return_code = process.wait(timeout=5.0)
        if return_code != -signal.SIGPIPE:
            raise AssertionError(
                f"broken stdout at {stage}: expected {-signal.SIGPIPE}, got {return_code}")
        assert_restored(slave_fd, original, f"broken stdout at {stage}")
        if b"9" in read_until(master_fd, b"\x00", 0.1):
            raise AssertionError(f"broken stdout at {stage}: terminal echoed PIN input")
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait()
        for fd in (read_fd, write_fd, master_fd, slave_fd):
            if fd >= 0:
                os.close(fd)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", default=None)
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    binary = Path(args.bin).resolve() if args.bin else root / "pdvrdt"
    image = root / "tests/golden/default_text/embedded.png"
    if not os.access(binary, os.X_OK):
        parser.error(f"binary is not executable: {binary}")

    for signal_number in (
        signal.SIGINT,
        signal.SIGTERM,
        signal.SIGHUP,
        signal.SIGQUIT,
        signal.SIGPIPE,
        signal.SIGTSTP,
    ):
        test_signal(binary, image, signal_number)
        print(f"[PASS] terminal restored before {signal.Signals(signal_number).name} re-delivery")
    test_masking(binary, image)
    print("[PASS] PIN input is masked and terminal modes are restored")
    for stage in ("prompt", "digit", "backspace", "newline"):
        test_broken_stdout(binary, image, stage)
        print(f"[PASS] terminal restored before broken stdout terminates at PIN {stage}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
