#!/usr/bin/env python3
"""Generate a synthetic text corpus for testing and benchmarking TwinLine.

The output is plain UTF-8 text with one sentence per line. A small share of
lines are planted duplicates so that the near-duplicate analysis has
something to find:

* exact copies of an earlier line,
* near copies with one or two words replaced,
* case variants (the same line in upper case or title case),
* lines with accented and Turkish letters, to exercise Unicode handling.

The output is deterministic for a given seed and size.

Examples:
    python3 scripts/gen_corpus.py --size 1MiB --output corpus/corpus-1MiB.txt
    python3 scripts/gen_corpus.py --size 10MiB --seed 7 -o corpus/big.txt
"""

from __future__ import annotations

import argparse
import random
import re
import sys
from pathlib import Path

NOUNS = (
    "server client request response thread lock queue buffer file line word "
    "document editor report analysis result error warning message process "
    "memory cache disk network packet socket kernel module library function "
    "variable pointer array string table index query record field value key "
    "user team project meeting deadline review commit branch release build "
    "test bug feature design idea question answer note list plan task day "
    "week month morning evening city river mountain forest garden window "
    "door kitchen coffee tea bread music song film game book teacher student"
).split()

VERBS = (
    "answered opened closed wrote read found fixed saved loaded sent received "
    "started finished checked updated deleted created merged reviewed tested "
    "waited returned changed moved copied printed parsed counted compared"
).split()

ADJECTIVES = (
    "new old big small fast slow long short quick quiet busy empty full "
    "simple complex shared private broken stable early late warm cold bright"
).split()

ADVERBS = "quickly slowly carefully again twice today yesterday finally".split()

SUFFIXES = ["", "", "", "s"]  # singular and plural nouns

UNICODE_WORDS = (
    "café naïve résumé façade jalapeño über straße İstanbul ılık çay şeker "
    "ğüzel öğrenci Ankara İzmir Çanakkale Ölüdeniz Ürgüp Iğdır"
).split()

DETERMINERS = ["the", "a", "every", "one", "this", "that", "our", "their"]

SIZE_PATTERN = re.compile(r"^\s*(\d+(?:\.\d+)?)\s*([KMG]i?B?|B)?\s*$", re.IGNORECASE)
UNITS = {"": 1, "B": 1, "K": 1024, "M": 1024**2, "G": 1024**3}


def parse_size(text: str) -> int:
    """Parse sizes such as '512KiB', '1MiB', '10M' or '2048'."""
    match = SIZE_PATTERN.match(text)
    if not match:
        raise argparse.ArgumentTypeError(f"invalid size: {text!r}")
    number, unit = match.groups()
    unit = (unit or "B")[0].upper()
    size = int(float(number) * UNITS[unit])
    if size <= 0:
        raise argparse.ArgumentTypeError("size must be positive")
    return size


class Generator:
    def __init__(self, seed: int, unicode_ratio: float) -> None:
        self.rng = random.Random(seed)
        self.unicode_ratio = unicode_ratio
        words = [noun + suffix for noun in NOUNS for suffix in SUFFIXES]
        self.rng.shuffle(words)
        self.words = words
        # Zipf-like weights: a few words are common, most are rare.
        self.weights = [1.0 / (rank + 1) ** 0.8 for rank in range(len(words))]

    def word(self) -> str:
        if self.rng.random() < self.unicode_ratio:
            return self.rng.choice(UNICODE_WORDS)
        return self.rng.choices(self.words, self.weights)[0]

    def sentence(self) -> str:
        length = self.rng.randint(6, 13)
        words = [self.word() for _ in range(length - 3)]
        words.insert(0, self.rng.choice(DETERMINERS))
        words.insert(self.rng.randrange(1, len(words)), self.rng.choice(ADJECTIVES))
        words.append(self.rng.choice(VERBS))
        if self.rng.random() < 0.3:
            words.append(self.rng.choice(ADVERBS))
        text = " ".join(words)
        return text[0].upper() + text[1:] + self.rng.choice([".", ".", ".", "!", "?"])

    def near_copy(self, line: str) -> str:
        words = line.split()
        for _ in range(self.rng.randint(1, 2)):
            words[self.rng.randrange(len(words))] = self.word()
        return " ".join(words)

    def case_variant(self, line: str) -> str:
        return line.upper() if self.rng.random() < 0.5 else line.title()

    def next_line(self, previous: list[str]) -> str:
        roll = self.rng.random()
        if previous and roll < 0.020:
            return self.near_copy(self.rng.choice(previous))
        if previous and roll < 0.025:
            return self.rng.choice(previous)
        if previous and roll < 0.028:
            return self.case_variant(self.rng.choice(previous))
        return self.sentence()


def generate(size: int, seed: int, unicode_ratio: float) -> str:
    gen = Generator(seed, unicode_ratio)
    lines: list[str] = []
    used = 0
    while True:
        line = gen.next_line(lines)
        cost = len(line.encode("utf-8")) + 1
        if used + cost > size:
            break
        lines.append(line)
        used += cost
    return "".join(line + "\n" for line in lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--size", type=parse_size, default=parse_size("1MiB"),
                        help="approximate output size, e.g. 512KiB, 1MiB, 10MiB (default: 1MiB)")
    parser.add_argument("--seed", type=int, default=461, help="random seed (default: 461)")
    parser.add_argument("--unicode-ratio", type=float, default=0.02,
                        help="share of words with non-ASCII letters (default: 0.02)")
    parser.add_argument("-o", "--output", type=Path,
                        help="output file (default: standard output)")
    args = parser.parse_args(argv)

    if not 0.0 <= args.unicode_ratio <= 1.0:
        parser.error("--unicode-ratio must be between 0 and 1")

    text = generate(args.size, args.seed, args.unicode_ratio)
    if args.output is None:
        sys.stdout.write(text)
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8", newline="\n")
        print(f"Wrote {len(text.encode('utf-8')):,} bytes, "
              f"{text.count(chr(10)):,} lines to {args.output}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
