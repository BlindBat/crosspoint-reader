#!/usr/bin/env python3
"""Generate the font-system suite's extra .cpfont fixtures.

Usage: gen_font_fixtures.py <output-dir>

Reuses the builders of scripts/generate_test_cpfonts.py (same glyph formulas,
same v4 layout) for coverage shapes that suite does not ship:
  cjk.cpfont  - Latin + U+4E00 so the CJK probe in SdCardFontSystem fires.
  wide.cpfont - 1260 contiguous glyphs (U+0100..U+05EB) so a page can be
                built from 400 tall glyphs and later pages from disjoint
                short ones, exercising the mini-arena underuse hysteresis.
"""

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))
from generate_test_cpfonts import Style, build_cpfont  # noqa: E402


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: gen_font_fixtures.py <output-dir>")
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)

    cjk = build_cpfont([Style(0, [(0x20, 0x7A), (0x4E00, 0x4E00), (0xFFFD, 0xFFFD)], 100, 0x40)])
    (out / "cjk.cpfont").write_bytes(cjk)

    wide = build_cpfont([Style(0, [(0x100, 0x100 + 1259), (0xFFFD, 0xFFFD)], 100, 0x40)])
    (out / "wide.cpfont").write_bytes(wide)


if __name__ == "__main__":
    main()
