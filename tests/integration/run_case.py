#!/usr/bin/env python3
"""Run one TwinLine integration test case and compare it with golden files.

A case is a directory under tests/integration/cases/ containing:

    stdin.txt            commands fed to twinline on standard input (optional)
    args.txt             command-line arguments, one per line (optional)
    files/               fixtures copied into the working directory (optional)
    expected/stdout.txt  expected standard output
    expected/stderr.txt  expected standard error (optional, default: empty)
    expected/exit.txt    expected exit status (optional, default: 0)
    expected/files/      files that must exist afterwards with this content

Timings ("Finished in 0.123 s") and the version ("TwinLine 1.2.3") are
replaced by placeholders before comparing, so results depend neither on the
machine nor on the release.

Usage:
    run_case.py --binary build/twinline --case tests/integration/cases/help
    run_case.py ... --update      # rewrite the golden files from this run

Set TWINLINE_TEST_WRAPPER (for example "valgrind --error-exitcode=99 -q")
to run the binary under a wrapper.
"""

from __future__ import annotations

import argparse
import difflib
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

TIMING = re.compile(r"Finished in \d+\.\d+ s")
VERSION = re.compile(r"TwinLine \d+\.\d+\.\d+")
TIMEOUT_SECONDS = 120


def normalise(text: str) -> str:
    text = TIMING.sub("Finished in <time>", text)
    return VERSION.sub("TwinLine <version>", text)


def read_text(path: Path, default: str = "") -> str:
    return path.read_text(encoding="utf-8") if path.exists() else default


def show_diff(label: str, expected: str, actual: str) -> None:
    diff = difflib.unified_diff(
        expected.splitlines(keepends=True),
        actual.splitlines(keepends=True),
        fromfile=f"expected {label}",
        tofile=f"actual {label}",
    )
    sys.stdout.writelines(diff)
    if not expected.endswith("\n") or not actual.endswith("\n"):
        sys.stdout.write("\n")


def run(binary: Path, case: Path, update: bool) -> int:
    args = [line for line in read_text(case / "args.txt").splitlines() if line]
    stdin = read_text(case / "stdin.txt")
    wrapper = shlex.split(os.environ.get("TWINLINE_TEST_WRAPPER", ""))

    with tempfile.TemporaryDirectory(prefix="twinline-it-") as tmp:
        workdir = Path(tmp)
        fixtures = case / "files"
        if fixtures.is_dir():
            shutil.copytree(fixtures, workdir, dirs_exist_ok=True)

        proc = subprocess.run(
            [*wrapper, str(binary.resolve()), *args],
            input=stdin,
            capture_output=True,
            text=True,
            encoding="utf-8",
            cwd=workdir,
            timeout=TIMEOUT_SECONDS,
            env={**os.environ, "LC_ALL": "C.UTF-8"},
        )
        actual = {
            "stdout.txt": normalise(proc.stdout),
            "stderr.txt": normalise(proc.stderr),
            "exit.txt": f"{proc.returncode}\n",
        }

        expected_dir = case / "expected"
        if update:
            expected_dir.mkdir(exist_ok=True)
            for name, content in actual.items():
                default = "0\n" if name == "exit.txt" else ""
                target = expected_dir / name
                if content == default and name != "stdout.txt":
                    target.unlink(missing_ok=True)
                else:
                    target.write_text(content, encoding="utf-8")
            print(f"updated {case.name}")
            return 0

        failed = False
        for name, content in actual.items():
            default = "0\n" if name == "exit.txt" else ""
            expected = read_text(expected_dir / name, default)
            if content != expected:
                print(f"--- {case.name}: {name} differs")
                show_diff(name, expected, content)
                failed = True

        expected_files = expected_dir / "files"
        if expected_files.is_dir():
            for want in sorted(p for p in expected_files.rglob("*") if p.is_file()):
                rel = want.relative_to(expected_files)
                got = workdir / rel
                if not got.exists():
                    print(f"--- {case.name}: expected file {rel} was not created")
                    failed = True
                elif got.read_bytes() != want.read_bytes():
                    print(f"--- {case.name}: file {rel} differs")
                    show_diff(str(rel), want.read_text(encoding="utf-8"),
                              got.read_text(encoding="utf-8", errors="replace"))
                    failed = True

    print(f"{'FAIL' if failed else 'ok'}: {case.name}")
    return 1 if failed else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--case", type=Path, required=True)
    parser.add_argument("--update", action="store_true", help="rewrite the golden files")
    args = parser.parse_args()
    return run(args.binary, args.case, args.update)


if __name__ == "__main__":
    sys.exit(main())
