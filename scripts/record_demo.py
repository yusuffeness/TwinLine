#!/usr/bin/env python3
"""Record docs/assets/demo.gif by driving the real TwinLine in a terminal.

TwinLine runs in a pseudo-terminal; the keys of a short session are typed at
a human pace, the screen is read through a terminal emulator (pyte) and every
change is drawn into a frame with its colours. Identical frames are merged.

Usage:
    python3 scripts/record_demo.py [--binary build/twinline] [--output docs/assets/demo.gif]

Needs: pip install pyte pillow, and DejaVu Sans Mono (fonts-dejavu-core).
"""

from __future__ import annotations

import argparse
import fcntl
import os
import pty
import select
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time
from pathlib import Path

import pyte
from PIL import Image, ImageDraw, ImageFont

COLS, ROWS = 100, 30
FONT_SIZE = 15
PAD = 14
BACKGROUND = (26, 27, 38)
FOREGROUND = (192, 202, 245)
FRAME_SECONDS = 0.1    # how often the screen is sampled
TYPE_SECONDS = 0.06    # between typed keys
FONT_DIRS = ["/usr/share/fonts/truetype/dejavu", "/Library/Fonts", str(Path.home() / "Library/Fonts")]

# Block elements of the progress bars, in eighths of a cell.
BLOCKS = {"\u2588": 8, "\u2589": 7, "\u258a": 6, "\u258b": 5, "\u258c": 4, "\u258d": 3,
          "\u258e": 2, "\u258f": 1}

# Rounded corners: the directions of their two arms (x, y).
CORNERS = {"\u256d": (1, 1), "\u256e": (-1, 1), "\u2570": (1, -1), "\u256f": (-1, -1)}

# The 16 basic colours, for programs that use them (TwinLine uses 24-bit).
NAMED = {
    "black": (21, 22, 30), "red": (247, 118, 142), "green": (158, 206, 106),
    "brown": (224, 175, 104), "yellow": (224, 175, 104), "blue": (122, 162, 247),
    "magenta": (187, 154, 247), "cyan": (125, 207, 255), "white": (192, 202, 245),
}

NOTES = """\
Deploy the new search service on Friday
Review the pull request for the login page
Buy coffee beans for the office
deploy the new search service on Friday!
Update the README with the install steps
Call the client about the invoice
Review the pull request for the login page.
Plan the roadmap meeting for next quarter
Update the README with install steps
Buy coffee beans for the office kitchen
Write tests for the export feature
Call the client about the late invoice
"""

CONFIG = """\
[analysis]
threshold = 0.6
threads = 2

[autosave]
enabled = false
"""


def find_font(name: str) -> str:
    for d in FONT_DIRS:
        p = Path(d) / name
        if p.exists():
            return str(p)
    sys.exit(f"record_demo.py: font {name} not found (install DejaVu Sans Mono)")


def colour(value: str, default: tuple[int, int, int]) -> tuple[int, int, int]:
    if value == "default":
        return default
    if len(value) == 6:
        try:
            return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))  # type: ignore[return-value]
        except ValueError:
            return default
    return NAMED.get(value, default)


class Recorder:
    def __init__(self, argv: list[str], cwd: str) -> None:
        self.screen = pyte.Screen(COLS, ROWS)
        self.stream = pyte.ByteStream(self.screen)
        self.frames: list[tuple[Image.Image, float]] = []  # image and the time it appeared
        self.last_key: tuple | None = None
        self.regular = ImageFont.truetype(find_font("DejaVuSansMono.ttf"), FONT_SIZE)
        self.bold = ImageFont.truetype(find_font("DejaVuSansMono-Bold.ttf"), FONT_SIZE)
        box = self.regular.getbbox("M")
        self.cell_w = self.regular.getlength("M")
        self.cell_h = int(FONT_SIZE * 1.3)
        self.ascent_fix = -box[1] // 2
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir(cwd)
            env = dict(os.environ, TERM="xterm-256color", COLORTERM="truecolor",
                       TWINLINE_CONFIG=os.path.join(cwd, "config.toml"))
            env.pop("NO_COLOR", None)
            os.execvpe(argv[0], argv, env)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))

    def pump(self, seconds: float) -> None:
        end = time.time() + seconds
        while time.time() < end:
            ready, _, _ = select.select([self.fd], [], [], FRAME_SECONDS)
            if ready:
                try:
                    self.stream.feed(os.read(self.fd, 65536))
                except OSError:
                    return
            self.capture()

    def capture(self) -> None:
        key = tuple(tuple((c.data, c.fg, c.bg, c.bold, c.reverse) for c in
                          (self.screen.buffer[y][x] for x in range(COLS))) for y in range(ROWS))
        cursor = (self.screen.cursor.x, self.screen.cursor.y)
        if (key, cursor) == self.last_key and self.frames:
            return
        self.last_key = (key, cursor)
        self.frames.append((self.render(cursor), time.time()))

    def cell_box(self, x: int, y: int) -> tuple[int, int, int, int]:
        """Pixel edges of a cell, rounded so that neighbours meet without gaps."""
        left = PAD + round(x * self.cell_w)
        right = PAD + round((x + 1) * self.cell_w)
        top = PAD + y * self.cell_h
        return left, top, right, top + self.cell_h

    def render(self, cursor: tuple[int, int]) -> Image.Image:
        width = PAD * 2 + round(COLS * self.cell_w)
        height = ROWS * self.cell_h + 2 * PAD
        img = Image.new("RGB", (width, height), BACKGROUND)
        draw = ImageDraw.Draw(img)
        for y in range(ROWS):
            row = self.screen.buffer[y]
            for x in range(COLS):
                c = row[x]
                fg = colour(c.fg, FOREGROUND)
                bg = colour(c.bg, BACKGROUND)
                if c.reverse:
                    fg, bg = bg, fg
                left, top, right, bottom = self.cell_box(x, y)
                if bg != BACKGROUND:
                    draw.rectangle([left, top, right - 1, bottom - 1], fill=bg)
                eighths = BLOCKS.get(c.data)
                if eighths is not None:
                    # Bar blocks as rectangles: glyphs leave seams between cells.
                    draw.rectangle([left, top, left + (right - left) * eighths // 8 - 1, bottom - 1],
                                   fill=fg)
                elif c.data == "\u2500":  # horizontal box line, drawn without seams
                    half = top + self.cell_h // 2
                    draw.line([(left, half), (right, half)], fill=fg, width=1)
                elif c.data == "\u2502":  # vertical box line
                    mid = left + (right - left) // 2
                    draw.line([(mid, top), (mid, bottom)], fill=fg, width=1)
                elif c.data in CORNERS:  # rounded box corners, matching the lines above
                    mid = left + (right - left) // 2
                    half = top + self.cell_h // 2
                    r = min(right - left, self.cell_h) // 3
                    to_x, to_y = CORNERS[c.data]   # which way the two arms go
                    end_x = right if to_x > 0 else left
                    end_y = bottom if to_y > 0 else top
                    draw.line([(mid, end_y), (mid, half + (r if to_y > 0 else -r))], fill=fg, width=1)
                    draw.line([(mid + (r if to_x > 0 else -r), half), (end_x, half)], fill=fg, width=1)
                    box = [mid if to_x > 0 else mid - 2 * r, half if to_y > 0 else half - 2 * r,
                           mid + 2 * r if to_x > 0 else mid, half + 2 * r if to_y > 0 else half]
                    start = {(1, 1): 180, (-1, 1): 270, (1, -1): 90, (-1, -1): 0}[(to_x, to_y)]
                    draw.arc(box, start, start + 90, fill=fg, width=1)
                elif c.data == "\u23bf":
                    # No common monospace font has this corner: draw it.
                    mid = left + (right - left) // 3
                    half = top + self.cell_h // 2
                    draw.line([(mid, top + 1), (mid, half), (right - 1, half)], fill=fg, width=1)
                elif c.data.strip():
                    draw.text((left, top + self.ascent_fix), c.data, fill=fg,
                              font=self.bold if c.bold else self.regular)
        if not self.screen.cursor.hidden:
            left, top, _, bottom = self.cell_box(*cursor)
            draw.rectangle([left, top + 2, left + 1, bottom - 3], fill=FOREGROUND)
        return img

    def text(self) -> str:
        return "\n".join(self.screen.display)

    def wait_for(self, needle: str, timeout: float) -> None:
        end = time.time() + timeout
        while needle not in self.text() and time.time() < end:
            self.pump(FRAME_SECONDS)

    def type(self, text: str, pause: float = 0.6) -> None:
        for ch in text:
            os.write(self.fd, ch.encode())
            self.pump(TYPE_SECONDS)
        self.pump(pause)

    def key(self, raw: bytes, pause: float) -> None:
        os.write(self.fd, raw)
        self.pump(pause)

    def save(self, path: Path) -> None:
        images = [f.convert("P", palette=Image.Palette.ADAPTIVE, colors=48) for f, _ in self.frames]
        starts = [t for _, t in self.frames]
        durations = [max(20, int((b - a) * 1000)) for a, b in zip(starts, starts[1:])]
        durations.append(4000)  # rest on the last frame before the loop restarts
        images[0].save(path, save_all=True, append_images=images[1:], duration=durations,
                       loop=0, optimize=True, disposal=1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", default="build/twinline")
    parser.add_argument("--output", default="docs/assets/demo.gif")
    args = parser.parse_args()
    binary = str(Path(args.binary).resolve())
    root = Path(__file__).resolve().parent.parent

    with tempfile.TemporaryDirectory(prefix="twinline-demo-") as tmp:
        Path(tmp, "notes.txt").write_text(NOTES, encoding="utf-8")
        Path(tmp, "config.toml").write_text(CONFIG, encoding="utf-8")
        subprocess.run([sys.executable, str(root / "scripts/gen_corpus.py"), "--size", "300KiB",
                        "--seed", "11", "--output", str(Path(tmp, "corpus.txt"))],
                       check=True, stdout=subprocess.DEVNULL)

        rec = Recorder([binary, "--exhaustive", "corpus.txt"], tmp)
        rec.pump(2.2)                            # the welcome banner and its shine
        rec.type("/thr", 0.4)                    # the menu filters as you type
        rec.key(b"\t", 0.2)
        rec.key(b"\r", 0.6)
        rec.type("/analyze", 0.1)
        rec.key(b"\r", 0.1)
        rec.wait_for("Analysis finished", 30)    # live progress, then the result card
        rec.pump(2.2)
        rec.type("/open! notes.txt", 0.1)
        rec.key(b"\r", 0.8)
        rec.type("/clusters", 0.1)
        rec.key(b"\r", 2.2)
        rec.type("/clusters 4", 0.1)
        rec.key(b"\r", 2.0)
        rec.type("/dedupe", 0.1)
        rec.key(b"\r", 1.2)
        rec.type("/show", 0.1)
        rec.key(b"\r", 2.5)
        os.kill(rec.pid, signal.SIGTERM)  # end on the editor, not on an empty prompt
        os.waitpid(rec.pid, 0)

    output = Path(args.output)
    rec.save(output)
    total = rec.frames[-1][1] - rec.frames[0][1] + 4.0
    print(f"{output}: {len(rec.frames)} frames, {total:.1f} s, {output.stat().st_size // 1024} KiB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
