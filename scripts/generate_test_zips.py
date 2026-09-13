#!/usr/bin/env python3
"""
Generate deterministic ZIP fixtures for the test/zip_file host suite.

Produces one well-formed archive and a family of malformed variants created by
byte-surgery on that base archive. The malformed variants exercise ZipFile's
error handling: every one must be rejected gracefully (no crash, no unbounded
allocation) by lib/ZipFile.

All entries use a fixed timestamp so the output bytes are reproducible; the
suite hard-codes the entry names and payloads that this script writes, so keep
GOOD_ENTRIES in sync with ZipFileTest.cpp if you change them.

Usage:
    python3 scripts/generate_test_zips.py            # writes test/zip_file/resources
    python3 scripts/generate_test_zips.py --print    # also print a manifest
"""

import os
import struct
import sys
import zipfile
from pathlib import Path

OUTPUT_DIR = Path(__file__).parent.parent / "test" / "zip_file" / "resources"
BOOK_OUTPUT_DIR = Path(__file__).parent.parent / "test" / "book_metadata_cache" / "resources"

FIXED_DATE = (2021, 1, 1, 0, 0, 0)  # deterministic mtime for every entry

# A minimal EPUB-shaped archive whose entry names double as spine hrefs in the
# BookMetadataCache suite. Uncompressed sizes are what buildBookBin sums into
# cumulative sizes, so keep these payload lengths in sync with BookMetadataCacheTest.
BOOK_ENTRIES = [
    ("OEBPS/chapter1.xhtml", b"<html><body>Chapter one.</body></html>", zipfile.ZIP_DEFLATED),
    ("OEBPS/chapter2.xhtml", b"<html><body>" + b"Chapter two. " * 8 + b"</body></html>", zipfile.ZIP_DEFLATED),
    ("OEBPS/chapter3.xhtml", b"<html><body>Chapter three, stored.</body></html>", zipfile.ZIP_STORED),
]

# (name, payload-bytes, compression-method)
# The deflated entry is deliberately repetitive so DEFLATE actually shrinks it,
# and the suite depends on these exact names and payloads.
GOOD_ENTRIES = [
    ("stored.txt", b"This entry is stored without compression.\n", zipfile.ZIP_STORED),
    ("deflated.txt", b"Deflate me! " * 64, zipfile.ZIP_DEFLATED),
    ("nested/deep.txt", b"Line of nested content.\n" * 16, zipfile.ZIP_DEFLATED),
]

EOCD_SIG = b"PK\x05\x06"
CDH_SIG = b"PK\x01\x02"
LFH_SIG = b"PK\x03\x04"


def build_good_zip() -> bytes:
    """Build the canonical well-formed archive in memory, deterministically."""
    import io

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        for name, payload, method in GOOD_ENTRIES:
            info = zipfile.ZipInfo(name, date_time=FIXED_DATE)
            info.compress_type = method
            # external_attr / create_system pinned so bytes are stable across OSes
            info.create_system = 3  # unix
            info.external_attr = 0o644 << 16
            zf.writestr(info, payload)
    return buf.getvalue()


def find_eocd(data: bytes) -> int:
    """Return the offset of the End Of Central Directory record."""
    idx = data.rfind(EOCD_SIG)
    if idx < 0:
        raise ValueError("no EOCD in base archive")
    return idx


def eocd_fields(data: bytes, eocd: int):
    """Return (total_entries, cd_size, cd_offset) from the EOCD record."""
    total_entries = struct.unpack_from("<H", data, eocd + 10)[0]
    cd_size = struct.unpack_from("<I", data, eocd + 12)[0]
    cd_offset = struct.unpack_from("<I", data, eocd + 16)[0]
    return total_entries, cd_size, cd_offset


def cd_entry_offsets(data: bytes, cd_offset: int):
    """Yield the absolute offset of each central-directory header."""
    pos = cd_offset
    while data[pos : pos + 4] == CDH_SIG:
        yield pos
        name_len = struct.unpack_from("<H", data, pos + 28)[0]
        extra_len = struct.unpack_from("<H", data, pos + 30)[0]
        comment_len = struct.unpack_from("<H", data, pos + 32)[0]
        pos += 46 + name_len + extra_len + comment_len


def make_truncated(good: bytes) -> bytes:
    """Cut the file in the middle of the central directory (drops the EOCD)."""
    eocd = find_eocd(good)
    _, cd_size, cd_offset = eocd_fields(good, eocd)
    # keep local data + half the central directory, nothing else
    cut = cd_offset + max(1, cd_size // 2)
    return good[:cut]


def make_eocd_offset_past_eof(good: bytes) -> bytes:
    """Valid EOCD, but its central-directory offset points well past EOF."""
    data = bytearray(good)
    eocd = find_eocd(data)
    struct.pack_into("<I", data, eocd + 16, 0x7FFFFFFF)  # ~2GB, far past EOF
    return bytes(data)


def make_local_central_mismatch(good: bytes) -> bytes:
    """Corrupt the first local file header signature; central dir stays intact.

    loadFileStatSlim still finds the entry via the central directory, but
    getDataOffset re-reads the local header and must reject the bad signature.
    """
    data = bytearray(good)
    lfh = data.find(LFH_SIG)
    if lfh < 0:
        raise ValueError("no local file header found")
    data[lfh : lfh + 4] = b"XXXX"
    return bytes(data)


def make_lying_uncompressed_size(good: bytes) -> bytes:
    """Overwrite the deflated entry's uncompressed size with a huge value.

    The declared size (~4GB) is a lie: the real payload is tiny. This is the
    classic 380KB-RAM killer -- a reader that trusts the field allocates a
    buffer far larger than the device has.
    """
    data = bytearray(good)
    huge = 0xFFFFFFF0
    eocd = find_eocd(data)
    _, _, cd_offset = eocd_fields(data, eocd)

    # Central directory: uncompressed size lives at CDH+24.
    for pos in cd_entry_offsets(data, cd_offset):
        name_len = struct.unpack_from("<H", data, pos + 28)[0]
        name = data[pos + 46 : pos + 46 + name_len].decode("latin1")
        if name == "deflated.txt":
            struct.pack_into("<I", data, pos + 24, huge)
            lho = struct.unpack_from("<I", data, pos + 42)[0]
            # Local file header: uncompressed size lives at LFH+22.
            if data[lho : lho + 4] == LFH_SIG:
                struct.pack_into("<I", data, lho + 22, huge)
            break
    return bytes(data)


def make_garbage_deflate(good: bytes) -> bytes:
    """Keep headers and sizes valid, but replace the DEFLATE payload with junk.

    The declared uncompressed size is legitimate/small, so no huge allocation is
    expected; the inflate step itself must fail cleanly.
    """
    data = bytearray(good)
    eocd = find_eocd(data)
    _, _, cd_offset = eocd_fields(data, eocd)
    for pos in cd_entry_offsets(data, cd_offset):
        name_len = struct.unpack_from("<H", data, pos + 28)[0]
        name = data[pos + 46 : pos + 46 + name_len].decode("latin1")
        comp_size = struct.unpack_from("<I", data, pos + 20)[0]
        lho = struct.unpack_from("<I", data, pos + 42)[0]
        if name == "deflated.txt" and data[lho : lho + 4] == LFH_SIG:
            lfh_name = struct.unpack_from("<H", data, lho + 26)[0]
            lfh_extra = struct.unpack_from("<H", data, lho + 28)[0]
            payload = lho + 30 + lfh_name + lfh_extra
            for i in range(comp_size):
                data[payload + i] = (0xA5 ^ i) & 0xFF
            break
    return bytes(data)


ZIP64_EOCD_SIG = b"PK\x06\x06"
ZIP64_LOCATOR_SIG = b"PK\x06\x07"


def make_zip64_sentinels(good: bytes) -> bytes:
    """Turn the base archive into a ZIP64 archive by byte surgery.

    A real ZIP64 archive stores the true entry count / central-directory offset
    in a ZIP64 End Of Central Directory record and leaves 0xFFFF / 0xFFFFFFFF
    sentinels in the legacy 32-bit EOCD. lib/ZipFile only parses the legacy
    EOCD, so this fixture makes it read those sentinels verbatim: a 65535 entry
    count and a central-directory offset well past EOF. The reader must handle
    that without crashing or reserving from the bogus count.
    """
    eocd = find_eocd(good)
    _, _, cd_offset = eocd_fields(good, eocd)

    # ZIP64 EOCD record (56 bytes): only the signature and a plausible size
    # field matter for a reader that scans for the legacy record.
    z64 = bytearray(56)
    z64[0:4] = ZIP64_EOCD_SIG
    struct.pack_into("<Q", z64, 4, 44)  # size of remaining record
    struct.pack_into("<H", z64, 12, 45)  # version made by
    struct.pack_into("<H", z64, 14, 45)  # version needed
    struct.pack_into("<Q", z64, 24, 3)  # total entries this disk
    struct.pack_into("<Q", z64, 32, 3)  # total entries
    struct.pack_into("<I", z64, 40, 0)  # cd size (low; kept simple)
    struct.pack_into("<Q", z64, 48, cd_offset)  # cd offset

    # ZIP64 EOCD locator (20 bytes).
    loc = bytearray(20)
    loc[0:4] = ZIP64_LOCATOR_SIG
    struct.pack_into("<I", loc, 4, 0)  # disk with zip64 eocd
    struct.pack_into("<Q", loc, 8, eocd)  # relative offset of zip64 eocd
    struct.pack_into("<I", loc, 16, 1)  # total disks

    # Legacy EOCD with ZIP64 sentinels.
    legacy = bytearray(good[eocd:])
    struct.pack_into("<H", legacy, 10, 0xFFFF)  # total entries -> sentinel
    struct.pack_into("<I", legacy, 12, 0xFFFFFFFF)  # cd size -> sentinel
    struct.pack_into("<I", legacy, 16, 0xFFFFFFFF)  # cd offset -> sentinel

    return good[:eocd] + bytes(z64) + bytes(loc) + bytes(legacy)


def build_empty_zip() -> bytes:
    """A valid archive with zero entries (bare EOCD)."""
    import io

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w"):
        pass
    return buf.getvalue()


def build_book_epub() -> bytes:
    """Build the EPUB-shaped archive used by the BookMetadataCache suite."""
    import io

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        for name, payload, method in BOOK_ENTRIES:
            info = zipfile.ZipInfo(name, date_time=FIXED_DATE)
            info.compress_type = method
            info.create_system = 3
            info.external_attr = 0o644 << 16
            zf.writestr(info, payload)
    return buf.getvalue()


def main():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    BOOK_OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    good = build_good_zip()

    book_epub = build_book_epub()
    with open(BOOK_OUTPUT_DIR / "book.epub", "wb") as fh:
        fh.write(book_epub)
    if "--print" in sys.argv:
        print(f"\nbook.epub -> {BOOK_OUTPUT_DIR}")
        for name, payload, method in BOOK_ENTRIES:
            kind = "STORED" if method == zipfile.ZIP_STORED else "DEFLATED"
            print(f"  {name:24s} {len(payload):5d} bytes  {kind}")

    outputs = {
        "good.zip": good,
        "truncated_central_dir.zip": make_truncated(good),
        "eocd_offset_past_eof.zip": make_eocd_offset_past_eof(good),
        "local_central_mismatch.zip": make_local_central_mismatch(good),
        "lying_uncompressed_size.zip": make_lying_uncompressed_size(good),
        "garbage_deflate.zip": make_garbage_deflate(good),
        "zip64.zip": make_zip64_sentinels(good),
        "empty.zip": build_empty_zip(),
        "not_a_zip.bin": b"This is definitely not a zip file, just plain text.\n" * 4,
    }

    for name, data in outputs.items():
        path = OUTPUT_DIR / name
        with open(path, "wb") as fh:
            fh.write(data)
        if "--print" in sys.argv:
            print(f"{name:32s} {len(data):6d} bytes")

    if "--print" in sys.argv:
        print("\nGood-archive entries:")
        for name, payload, method in GOOD_ENTRIES:
            kind = "STORED" if method == zipfile.ZIP_STORED else "DEFLATED"
            print(f"  {name:20s} {len(payload):5d} bytes  {kind}")


if __name__ == "__main__":
    main()
