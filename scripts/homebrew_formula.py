#!/usr/bin/env python3
"""Fill in packaging/homebrew/twinline.rb.in for one release.

Usage:
    scripts/homebrew_formula.py VERSION SHA256 [TEMPLATE] > twinline.rb

SHA256 is the checksum of the source archive GitHub serves for the tag:
    curl -sL https://github.com/yusuffeness/TwinLine/archive/refs/tags/vVERSION.tar.gz | sha256sum
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) not in (3, 4):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    version = sys.argv[1].removeprefix("v")
    sha256 = sys.argv[2].lower()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version) or not re.fullmatch(r"[0-9a-f]{64}", sha256):
        print("homebrew_formula.py: expected VERSION like 1.2.3 and a 64-digit SHA256",
              file=sys.stderr)
        return 2
    template = Path(sys.argv[3] if len(sys.argv) == 4 else "packaging/homebrew/twinline.rb.in")
    text = template.read_text(encoding="utf-8")
    text = text.replace("@VERSION@", version).replace("@SHA256@", sha256)
    # The header comment explains the template, not the finished formula.
    lines = text.splitlines(keepends=True)
    while lines and lines[0].startswith("#"):
        lines.pop(0)
    sys.stdout.write("".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
