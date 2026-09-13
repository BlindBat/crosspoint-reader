#!/usr/bin/env python3
"""Generate deterministic .cpfont fixtures for test/sdcard_font.

Usage: generate_test_cpfonts.py <output-dir>

Writes small v4 .cpfont files (format: lib/EpdFont/scripts/fontconvert_sdcard.py,
reader: lib/EpdFont/SdCardFont.cpp) plus byte-surgery malformed variants.
Everything is derived from fixed formulas — no randomness, no timestamps —
so repeated runs produce byte-identical files. *.cpfont is gitignored, so
these are generated at test-build time by test/sdcard_font/CMakeLists.txt.

Glyph formulas (asserted by SdCardFontTest.cpp — keep in sync):
  width      = 4                    (1 bitmap byte per row at 2bpp)
  height     = (index % 3) + 1
  advanceX   = styleBase + index    (12.4 fixed-point; styleBase: 100 regular,
                                     200 italic)
  left       = (index % 3) - 1
  top        = 10
  dataLength = height
  bitmap[k]  = (bitmapBase + index * 7 + k * 3) & 0xFF
               (bitmapBase: 0x40 regular, 0x80 italic)
"""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "lib" / "EpdFont" / "scripts"))
from cpfont_version import CPFONT_VERSION  # noqa: E402

MAGIC = b"CPFONT\x00\x00"
HEADER_SIZE = 32
TOC_ENTRY_SIZE = 32
GLYPH_FORMAT = "<BBHhhH2xI"  # EpdGlyph, 16 bytes
TOC_FORMAT = "<B3xIIBhhHHBBBI4x"


def make_glyphs(intervals, style_base, bitmap_base):
    """Build (glyph_records, bitmap_blob) for the glyph count implied by intervals."""
    count = sum(last - first + 1 for first, last in intervals)
    glyphs = bytearray()
    bitmaps = bytearray()
    offset = 0
    for i in range(count):
        height = (i % 3) + 1
        data_length = height
        glyphs += struct.pack(GLYPH_FORMAT, 4, height, style_base + i,
                              (i % 3) - 1, 10, data_length, offset)
        for k in range(data_length):
            bitmaps.append((bitmap_base + i * 7 + k * 3) & 0xFF)
        offset += data_length
    return bytes(glyphs), bytes(bitmaps)


def pack_intervals(intervals):
    data = bytearray()
    offset = 0
    for first, last in intervals:
        data += struct.pack("<III", first, last, offset)
        offset += last - first + 1
    return bytes(data)


class Style:
    def __init__(self, style_id, intervals, style_base, bitmap_base,
                 advance_y=20, ascender=14, descender=-4,
                 kern_left=(), kern_right=(), kern_matrix=(),
                 kern_left_classes=0, kern_right_classes=0, ligatures=()):
        self.style_id = style_id
        self.intervals = intervals
        self.advance_y = advance_y
        self.ascender = ascender
        self.descender = descender
        self.kern_left = kern_left        # [(codepoint, classId)]
        self.kern_right = kern_right
        self.kern_matrix = kern_matrix    # flat leftClasses x rightClasses int8
        self.kern_left_classes = kern_left_classes
        self.kern_right_classes = kern_right_classes
        self.ligatures = ligatures        # [(leftCp, rightCp, ligCp)]
        self.glyphs, self.bitmaps = make_glyphs(intervals, style_base, bitmap_base)

    def sections(self):
        iv = pack_intervals(self.intervals)
        kl = b"".join(struct.pack("<HB", cp, cls) for cp, cls in self.kern_left)
        kr = b"".join(struct.pack("<HB", cp, cls) for cp, cls in self.kern_right)
        km = struct.pack(f"<{len(self.kern_matrix)}b", *self.kern_matrix) if self.kern_matrix else b""
        lig = b"".join(struct.pack("<II", (l << 16) | r, out) for l, r, out in self.ligatures)
        return iv, self.glyphs, kl, kr, km, lig, self.bitmaps

    def toc_entry(self, data_offset):
        glyph_count = len(self.glyphs) // 16
        return struct.pack(TOC_FORMAT, self.style_id, len(self.intervals), glyph_count,
                           self.advance_y, self.ascender, self.descender,
                           len(self.kern_left), len(self.kern_right),
                           self.kern_left_classes, self.kern_right_classes,
                           len(self.ligatures), data_offset)


def build_cpfont(styles, version=CPFONT_VERSION, style_count=None):
    header = struct.pack("<8sHHB19s", MAGIC, version, 1,
                         style_count if style_count is not None else len(styles), bytes(19))
    packed = [s.sections() for s in styles]
    data_start = HEADER_SIZE + len(styles) * TOC_ENTRY_SIZE
    toc = bytearray()
    offset = data_start
    for style, secs in zip(styles, packed):
        toc += style.toc_entry(offset)
        offset += sum(len(sec) for sec in secs)
    blob = bytearray(header)
    blob += toc
    for secs in packed:
        for sec in secs:
            blob += sec
    return bytes(blob)


def basic_style(style_id=0, style_base=100, bitmap_base=0x40, advance_y=20, with_kern_lig=True):
    intervals = [(0x20, 0x7A), (0xFB01, 0xFB01), (0xFFFD, 0xFFFD)]
    kwargs = {}
    if with_kern_lig:
        kwargs = dict(
            kern_left=[(ord("A"), 1), (ord("T"), 2)],
            kern_right=[(ord("."), 2), (ord("V"), 1)],
            kern_matrix=[-16, -8, -4, 0],  # rows: left class 1..2, cols: right class 1..2
            kern_left_classes=2,
            kern_right_classes=2,
            ligatures=[(ord("f"), ord("i"), 0xFB01)],
        )
    return Style(style_id, intervals, style_base, bitmap_base, advance_y=advance_y, **kwargs)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: generate_test_cpfonts.py <output-dir>")
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)

    def write(name, blob):
        (out / name).write_bytes(blob)

    valid = build_cpfont([basic_style()])
    write("valid_basic.cpfont", valid)

    # Two styles: regular (kern+lig) and italic (plain, different metrics).
    write("valid_multistyle.cpfont", build_cpfont([
        basic_style(style_id=0),
        basic_style(style_id=2, style_base=200, bitmap_base=0x80, advance_y=21, with_kern_lig=False),
    ]))

    # Astral coverage forces the full 12-byte interval representation.
    # Intervals must be sorted ascending (the loader validates this), so the
    # replacement glyph precedes the emoji block: U+FFFD = glyph 0,
    # U+1F600..03 = glyphs 1..4.
    write("valid_astral.cpfont", build_cpfont([
        Style(0, [(0xFFFD, 0xFFFD), (0x1F600, 0x1F603)], 300, 0xC0),
    ]))

    # --- Malformed variants (byte surgery on the valid file) ---

    def patched(offset, payload):
        blob = bytearray(valid)
        blob[offset:offset + len(payload)] = payload
        return bytes(blob)

    write("bad_magic.cpfont", patched(0, b"X"))
    write("bad_version.cpfont", patched(8, struct.pack("<H", CPFONT_VERSION + 1)))
    write("zero_styles.cpfont", patched(12, b"\x00"))
    write("too_many_styles.cpfont", patched(12, b"\x05"))
    write("style_id_oob.cpfont", patched(HEADER_SIZE + 0, b"\x07"))
    write("truncated_header.cpfont", valid[:16])
    write("truncated_toc.cpfont", valid[:HEADER_SIZE + 10])

    # TOC field offsets (see TOC_FORMAT): intervalCount at +4, glyphCount at +8,
    # kernLeftEntryCount at +17.
    write("huge_interval_count.cpfont", patched(HEADER_SIZE + 4, struct.pack("<I", 5000)))
    write("huge_glyph_count.cpfont", patched(HEADER_SIZE + 8, struct.pack("<I", 70000)))
    write("huge_kern_count.cpfont", patched(HEADER_SIZE + 17, struct.pack("<H", 5000)))

    intervals_off = HEADER_SIZE + TOC_ENTRY_SIZE
    # Interval 0 (first=0x20, last=0x7A, offset=0), each field u32.
    write("interval_first_gt_last.cpfont",
          patched(intervals_off, struct.pack("<II", 0x7A, 0x20)))
    # Interval 1 re-covers codepoints of interval 0 (duplicate codepoints
    # across intervals). Built as a fully consistent font otherwise — offsets
    # are cumulative and within the glyph count — so the ONLY violation is the
    # overlap itself and the loader must reject specifically that.
    write("interval_overlap.cpfont", build_cpfont([
        Style(0, [(0x20, 0x7A), (0x30, 0x31), (0xFFFD, 0xFFFD)], 100, 0x40),
    ]))
    # Interval 1 offset lies (90 instead of the expected 91).
    write("interval_offset_mismatch.cpfont",
          patched(intervals_off + 12 + 8, struct.pack("<I", 90)))
    # Interval 0 span (0x20..0x120) exceeds the style's 93 glyphs.
    write("interval_span_too_big.cpfont",
          patched(intervals_off + 4, struct.pack("<I", 0x120)))
    # Glyph count under-declared (92 instead of the 93 the intervals imply).
    # Each interval alone stays valid — first <= last, span <= glyphCount
    # (so the unsigned "glyphCount - span" in the loader cannot wrap), no
    # overlap, cumulative offsets — but the LAST interval's offset (92)
    # indexes one past the declared 92-glyph table: only the offset-overrun
    # check stands between load() and out-of-range glyph indices.
    write("interval_offset_overrun.cpfont",
          patched(HEADER_SIZE + 8, struct.pack("<I", 92)))

    write("truncated_intervals.cpfont", valid[:intervals_off + 20])

    glyphs_off = intervals_off + 3 * 12
    # Keep 2 whole glyph records: load() succeeds (it stops at intervals),
    # prewarm hits the truncation.
    write("truncated_glyphs.cpfont", valid[:glyphs_off + 2 * 16])

    # Structurally valid but the file ends inside the bitmap section, so most
    # glyphs' (dataOffset, dataLength) point past EOF.
    bitmaps_len = len(basic_style().bitmaps)
    write("bitmap_past_eof.cpfont", valid[:len(valid) - bitmaps_len + 4])


if __name__ == "__main__":
    main()
