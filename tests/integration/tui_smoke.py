#!/usr/bin/env python3
"""End-to-end test of the terminal UI in a pseudo-terminal.

Starts twinline in a pty (so it runs the interactive UI, not the plain REPL),
types keys like a user would and checks what appears on the terminal.

Session 1: welcome banner, command menu, Tab completion, saving a file,
reverse history search, the shortcuts list, the unsaved-changes guard,
exiting with Ctrl+C twice, and restoring the terminal.

Session 2: a background analysis reported while the UI is live, closing the
menu with Esc, the file completion menu, history browsing, bracketed paste,
word-wise cursor movement, Ctrl+L and quitting with quit!.

Usage: tui_smoke.py --binary build/twinline
"""

from __future__ import annotations

import argparse
import os
import re
import select
import shlex
import sys
import tempfile
import time

try:
    import fcntl
    import pty
    import struct
    import termios
except ImportError:  # not a POSIX system
    print("skipped: no pty support")
    sys.exit(0)

ANSI = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")
TIMEOUT = float(os.environ.get("TWINLINE_TUI_TIMEOUT", "20"))


class Session:
    def __init__(self, argv: list[str], cwd: str) -> None:
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir(cwd)
            env = dict(os.environ, TERM="xterm-256color", COLORTERM="truecolor")
            env.pop("NO_COLOR", None)
            os.execvpe(argv[0], argv, env)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 100, 0, 0))
        self.raw = b""

    def read_until(self, text: str) -> None:
        deadline = time.time() + TIMEOUT
        while text not in self.plain():
            remaining = deadline - time.time()
            if remaining <= 0:
                raise AssertionError(f"timed out waiting for {text!r}\n--- screen ---\n"
                                     f"{self.plain()[-3000:]}")
            ready, _, _ = select.select([self.fd], [], [], remaining)
            if ready:
                try:
                    chunk = os.read(self.fd, 65536)
                except OSError:
                    chunk = b""
                if not chunk:
                    if text not in self.plain():
                        raise AssertionError(f"program ended before {text!r}\n{self.plain()}")
                    return
                self.raw += chunk

    def plain(self) -> str:
        return ANSI.sub("", self.raw.decode("utf-8", errors="replace"))

    def send(self, keys: str) -> None:
        os.write(self.fd, keys.encode("utf-8"))
        time.sleep(0.15)

    def wait_exit(self) -> int:
        deadline = time.time() + TIMEOUT
        while time.time() < deadline:
            pid, status = os.waitpid(self.pid, os.WNOHANG)
            if pid != 0:
                # Drain whatever was written last.
                while True:
                    ready, _, _ = select.select([self.fd], [], [], 0.2)
                    if not ready:
                        break
                    try:
                        chunk = os.read(self.fd, 65536)
                    except OSError:
                        break
                    if not chunk:
                        break
                    self.raw += chunk
                return os.waitstatus_to_exitcode(status)
            time.sleep(0.05)
        raise AssertionError("program did not exit")


def session_one(argv: list[str], tmp: str) -> None:
    s = Session(argv, tmp)

    s.read_until("Welcome to TwinLine")
    s.read_until("Type a command, or / for the menu")

    s.send("/append hello world\r")
    s.read_until("Line 1 added.")

    s.send("/sa")                    # the menu filters down to /save
    s.read_until("/save")
    s.send("\t")                     # Tab completes "/save "
    s.send("out.txt\r")
    s.read_until("Saved 1 line.")
    with open(os.path.join(tmp, "out.txt"), encoding="utf-8") as f:
        assert f.read() == "hello world\n", "saved file has the wrong content"

    s.send("\x12app")                # Ctrl+R, search for "app"
    s.read_until("reverse search: app")
    s.send("\r")                     # run the match again
    s.read_until("Line 2 added.")

    s.send("?")
    s.read_until("Keyboard shortcuts")

    s.send("/quit\r")
    s.read_until("you have unsaved changes")
    s.send("\x03")                   # Ctrl+C once: a hint
    s.read_until("Press Ctrl+C again to exit")
    s.send("\x03")                   # Ctrl+C twice: quit (armed, so it exits)

    code = s.wait_exit()
    assert code == 0, f"exit status {code}"
    assert b"\x1b[?2004l" in s.raw, "terminal was not restored (bracketed paste still on)"


def session_two(argv: list[str], tmp: str) -> None:
    os.makedirs(os.path.join(tmp, "notes"))
    for name in ("draft-one.txt", "draft-two.txt"):
        with open(os.path.join(tmp, "notes", name), "w", encoding="utf-8") as f:
            f.write(f"{name}\n")
    with open(os.path.join(tmp, "data.txt"), "w", encoding="utf-8") as f:
        f.write("the server answered\nThe server answered!\nsomething else entirely\n")

    s = Session([*argv, "--threads=2", "data.txt"], tmp)
    s.read_until("Opened 'data.txt' (3 lines).")

    s.send("/analyze\r")
    s.read_until("Background analysis finished")
    s.read_until("Identical pairs             : 1")

    s.send("/op")
    s.read_until("/open")
    s.send("\x1b")                   # Esc closes the menu
    s.send("en! notes/dr\t")         # two matches: common prefix + file menu
    s.read_until("2 matches")
    s.send("\x1b[B\t\r")            # Down, Tab to choose, Enter to run
    s.read_until("Opened 'notes/draft-two.txt' (1 line).")

    s.send("\x1b[A")                 # Up: the previous command comes back
    s.read_until("> /open! notes/draft-two.txt")
    s.send("\x15")                   # Ctrl+U clears the line

    s.send("\x1b[200~/append pasted\nline\x1b[201~\r")  # newline in a paste is a space
    s.read_until("Line 2 added.")

    s.send("/append abc def\x1b[1;5DX\r")  # Ctrl+Left, then insert before "def"
    s.read_until("Line 3 added.")

    s.send("\x0c")                   # Ctrl+L clears the screen
    s.send("/show\r")
    s.read_until("pasted line")
    s.read_until("abc Xdef")

    s.send("/quit!\r")
    code = s.wait_exit()
    assert code == 0, f"exit status {code}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    wrapper = shlex.split(os.environ.get("TWINLINE_TEST_WRAPPER", ""))
    argv = [*wrapper, os.path.abspath(args.binary)]

    with tempfile.TemporaryDirectory(prefix="twinline-tui-") as tmp:
        session_one(argv, tmp)
    print("ok: session 1 (menu, completion, save, search, guard, exit)")
    with tempfile.TemporaryDirectory(prefix="twinline-tui-") as tmp:
        session_two(argv, tmp)
    print("ok: session 2 (background result, file menu, history, paste, words)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as error:
        print(f"FAIL: {error}")
        sys.exit(1)
