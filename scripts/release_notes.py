#!/usr/bin/env python3
"""Print the CHANGELOG.md section of one version, for a GitHub release.

Usage:
    scripts/release_notes.py 1.0.0 [CHANGELOG.md]

Exits with 1 if the version has no section, so a release cannot go out
without notes.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


def section(text: str, version: str) -> str | None:
    heading = re.compile(r"^## \[(?P<v>[^\]]+)\]", re.MULTILINE)
    matches = list(heading.finditer(text))
    for i, m in enumerate(matches):
        if m.group("v") == version:
            end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
            body = text[m.end():end]
            # Drop the date on the heading line and the link references.
            body = body.split("\n", 1)[1] if "\n" in body else ""
            body = re.sub(r"^\[[^\]]+\]: .*$", "", body, flags=re.MULTILINE)
            return body.strip() + "\n"
    return None


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    version = sys.argv[1].removeprefix("v")
    path = Path(sys.argv[2] if len(sys.argv) == 3 else "CHANGELOG.md")
    notes = section(path.read_text(encoding="utf-8"), version)
    if notes is None:
        print(f"release_notes.py: no section for {version} in {path}", file=sys.stderr)
        return 1
    sys.stdout.write(notes)
    return 0


if __name__ == "__main__":
    sys.exit(main())
