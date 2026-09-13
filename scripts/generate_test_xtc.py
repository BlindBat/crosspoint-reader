#!/usr/bin/env python3
"""Generate deterministic XTC/XTCH fixtures for the host-side xtc_parser test suite.

Produces a set of tiny container files under test/xtc_parser/fixtures/ (or the
directory given as argv[1]): one minimal valid file per format plus a series of
deliberately malformed variants exercising XtcParser's untrusted-input handling.

Format reference: lib/Xtc/Xtc/XtcTypes.h and docs in lib/Xtc/README.
The output is byte-for-byte deterministic (no timestamps, no randomness), so the
checked-in fixtures can be regenerated and diffed.
"""

import struct
import sys
from pathlib import Path

XTC_MAGIC = 0x00435458  # "XTC\0"
XTCH_MAGIC = 0x48435458  # "XTCH"
XTG_MAGIC = 0x00475458  # "XTG\0"
XTH_MAGIC = 0x00485458  # "XTH\0"

HEADER_SIZE = 56
TITLE_SIZE = 128  # at 0x38
AUTHOR_SIZE = 64  # at 0xB8
PAGE_TABLE_ENTRY_SIZE = 16
PAGE_HEADER_SIZE = 22
CHAPTER_RECORD_SIZE = 96

PAGE_W = 16
PAGE_H = 4


def build_header(magic, page_count, page_table_offset, data_offset,
                 version=(1, 0), has_metadata=1, has_chapters=0,
                 chapter_offset=0, metadata_offset=0x38):
    return struct.pack(
        "<IBBHBBBBIQQQQII",
        magic,
        version[0], version[1],
        page_count,
        0,              # readDirection
        has_metadata,
        0,              # hasThumbnails
        has_chapters,
        1,              # currentPage (1-based)
        metadata_offset if has_metadata else 0,
        page_table_offset,
        data_offset,
        0,              # thumbOffset
        chapter_offset,
        0,              # padding (readChapters reads offset+padding as one u64)
    )


def build_metadata(title=b"Minimal Test Book", author=b"CrossPoint QA"):
    return title.ljust(TITLE_SIZE, b"\0") + author.ljust(AUTHOR_SIZE, b"\0")


def page_bitmap(seed, bitmap_size):
    return bytes((seed + i) & 0xFF for i in range(bitmap_size))


def build_page(page_magic, bitmap, width=PAGE_W, height=PAGE_H):
    header = struct.pack("<IHHBBIQ", page_magic, width, height, 0, 0, len(bitmap), 0)
    return header + bitmap


def build_chapter(name, start_page, end_page):
    record = name.encode("utf-8").ljust(0x50, b"\0")
    record += struct.pack("<HH", start_page, end_page)
    return record.ljust(CHAPTER_RECORD_SIZE, b"\0")


def build_container(magic, page_seeds, chapters=None, page_count_override=None,
                    version=(1, 0), page_table_offset_override=None,
                    entry_offset_override=None):
    """Assemble header + metadata [+ chapters] + page table + page data."""
    if magic == XTCH_MAGIC:
        page_magic = XTH_MAGIC
        bitmap_size = ((PAGE_W * PAGE_H + 7) // 8) * 2  # two bit planes
    else:
        page_magic = XTG_MAGIC
        bitmap_size = ((PAGE_W + 7) // 8) * PAGE_H  # row-major, 8 px/byte

    chapters = chapters or []
    chapter_blob = b"".join(chapters)
    chapter_offset = HEADER_SIZE + TITLE_SIZE + AUTHOR_SIZE if chapters else 0
    page_table_offset = HEADER_SIZE + TITLE_SIZE + AUTHOR_SIZE + len(chapter_blob)
    data_offset = page_table_offset + len(page_seeds) * PAGE_TABLE_ENTRY_SIZE

    pages = [build_page(page_magic, page_bitmap(seed, bitmap_size)) for seed in page_seeds]

    table = b""
    offset = data_offset
    for page in pages:
        entry_offset = offset if entry_offset_override is None else entry_offset_override
        table += struct.pack("<QIHH", entry_offset, len(page), PAGE_W, PAGE_H)
        offset += len(page)

    header = build_header(
        magic,
        len(page_seeds) if page_count_override is None else page_count_override,
        page_table_offset if page_table_offset_override is None else page_table_offset_override,
        data_offset,
        version=version,
        has_chapters=1 if chapters else 0,
        chapter_offset=chapter_offset,
    )
    return header + build_metadata() + chapter_blob + table + b"".join(pages)


def main():
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else \
        Path(__file__).resolve().parent.parent / "test" / "xtc_parser" / "fixtures"
    out_dir.mkdir(parents=True, exist_ok=True)

    def emit(name, blob):
        (out_dir / name).write_bytes(blob)
        print(f"{name}: {len(blob)} bytes")

    minimal = build_container(XTC_MAGIC, page_seeds=[0xA0, 0xB0])
    emit("minimal.xtc", minimal)

    emit("minimal.xtch", build_container(XTCH_MAGIC, page_seeds=[0xC0, 0xD0]))

    # Hostile / malformed variants ------------------------------------------------

    # 0-byte and mid-header truncations: header read must fail cleanly.
    emit("empty.xtc", b"")
    emit("truncated_header.xtc", minimal[:20])

    # pageCount == 0 is rejected as corrupted.
    emit("zero_pages.xtc", build_container(XTC_MAGIC, page_seeds=[0xA0, 0xB0], page_count_override=0))

    # Header claims far more pages than the page table (and file) can hold.
    emit("page_count_lie.xtc", build_container(XTC_MAGIC, page_seeds=[0xA0, 0xB0], page_count_override=500))

    # Subtler lie: 6 claimed pages need a 96-byte table, which fits inside the
    # 340-byte file but not between the table offset (248) and EOF. Catches a
    # bounds check that forgets to subtract the table offset.
    emit("page_count_lie_subtle.xtc", build_container(XTC_MAGIC, page_seeds=[0xA0, 0xB0], page_count_override=6))

    # Absurd page count. The header field is uint16_t, so 0xFFFF is the largest
    # representable lie (a u32-max count cannot even be encoded); the claimed
    # 1MB page table must be rejected against the tiny real file size before
    # any allocation happens.
    emit("absurd_page_count.xtc", build_container(XTC_MAGIC, page_seeds=[0xA0, 0xB0], page_count_override=0xFFFF))

    # Not an XTC file at all, but long enough that the 56-byte header read succeeds.
    emit("bad_magic.xtc", b"This is not an XTC container, just some plain text padding.....")

    # Unsupported format version (2.0).
    emit("bad_version.xtc", build_container(XTC_MAGIC, page_seeds=[0xA0, 0xB0], version=(2, 0)))

    # Page table offset of 0 (inside what would be the header).
    emit("zero_table_offset.xtc", build_container(XTC_MAGIC, page_seeds=[0xA0, 0xB0],
                                                  page_table_offset_override=0))

    # Page table entry whose data offset points far past EOF.
    single = build_container(XTC_MAGIC, page_seeds=[0xA0])
    emit("offset_past_eof.xtc", build_container(XTC_MAGIC, page_seeds=[0xA0],
                                                entry_offset_override=len(single) + 4096))

    # Both page table entries resolve to the same data region (overlapping ranges).
    two = build_container(XTC_MAGIC, page_seeds=[0xA0, 0xB0])
    first_data_offset = HEADER_SIZE + TITLE_SIZE + AUTHOR_SIZE + 2 * PAGE_TABLE_ENTRY_SIZE
    emit("overlap_pages.xtc", build_container(XTC_MAGIC, page_seeds=[0xA0, 0xB0],
                                              entry_offset_override=first_data_offset))
    assert len(two) == len(minimal)

    # Page header lies about its dimensions: claims a full 480x800 page (48000
    # bytes of bitmap) while the file only contains the usual tiny bitmap.
    lie_page = struct.pack("<IHHBBIQ", XTG_MAGIC, 480, 800, 0, 0, 48000, 0) + page_bitmap(0xA0, 8)
    table_offset = HEADER_SIZE + TITLE_SIZE + AUTHOR_SIZE
    data_offset = table_offset + PAGE_TABLE_ENTRY_SIZE
    dims_lie = build_header(XTC_MAGIC, 1, table_offset, data_offset) + build_metadata()
    dims_lie += struct.pack("<QIHH", data_offset, len(lie_page), 480, 800)
    dims_lie += lie_page
    emit("page_dims_lie.xtc", dims_lie)

    # Chapters: one valid, one with an end page past the book (must clamp), one
    # starting past the book (must be dropped). Pages are stored 1-based.
    chapters = [
        build_chapter("Intro", 1, 2),
        build_chapter("Overflow End", 4, 999),
        build_chapter("Past The End", 200, 210),
    ]
    emit("chapters.xtc", build_container(XTC_MAGIC, page_seeds=[0xA0, 0xB0, 0xC0, 0xD0], chapters=chapters))


if __name__ == "__main__":
    main()
