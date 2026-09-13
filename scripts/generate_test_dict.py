#!/usr/bin/env python3
"""Generate the StarDict / dictzip fixtures for test/dictionary and test/dict_zip.

Deterministic: fixed gzip MTIME/XFL/OS bytes, fixed zlib level, no randomness.
Output goes to test/dict_common/resources/ and is checked in; rerun this script
only when the fixture set itself changes.

Formats implemented here (mirrors what src/util/Dictionary.cpp and
src/util/DictZip.cpp parse):

  .idx       sorted entries: word bytes, NUL, BE32 offset, BE32 size
             (sorted by ASCII-case-insensitive bytewise compare, the same
             order StringUtils::asciiCaseCmp expects)
  .syn       sorted entries: word bytes, NUL, BE32 ordinal (N-th .idx entry)
  .dict      concatenated definition payloads (offsets/sizes in the .idx)
  .dict.dz   gzip (RFC 1952) with an FEXTRA 'RA' subfield:
             LE16 version=1, LE16 chunkLength, LE16 chunkCount,
             then chunkCount LE16 compressed chunk sizes. The payload is one
             raw-deflate stream where every chunk boundary is a Z_FULL_FLUSH,
             so each chunk decompresses independently.

Every well-formed .dz produced here is self-verified two ways before being
written: whole-stream gunzip and per-chunk random-access inflation, both
compared byte-exact against the payload. The production reader's happy path is
additionally covered by the C++ suites (byte-exact extraction assertions).
"""

from __future__ import annotations

import gzip
import struct
import sys
import zlib
from pathlib import Path

RESOURCES = Path(__file__).resolve().parent.parent / "test" / "dict_common" / "resources"


def ascii_lower_key(word: str) -> bytes:
    """Sort key equivalent to StringUtils::asciiCaseCmp (ASCII-only tolower)."""
    return bytes(b + 32 if 0x41 <= b <= 0x5A else b for b in word.encode("utf-8"))


def definition_text(word: str) -> bytes:
    """Definition payload formula. Duplicated in the C++ suites -- keep in sync."""
    return f"{word}: definition text for {word}.\n".encode("utf-8")


def boundary_definition() -> bytes:
    """5004-byte position-marked payload (556 * '%08d-'); spans many 512B chunks."""
    return b"".join(b"%08d-" % i for i in range(556))


def build_dict_and_idx(words: list[str], defs: dict[str, bytes]) -> tuple[bytes, bytes, dict[str, tuple[int, int]]]:
    """Concatenate definitions in sorted-entry order; return (.dict, .idx, locations)."""
    ordered = sorted(words, key=ascii_lower_key)
    dict_data = b""
    idx_data = b""
    locations: dict[str, tuple[int, int]] = {}
    for w in ordered:
        payload = defs[w]
        off, size = len(dict_data), len(payload)
        locations[w] = (off, size)
        dict_data += payload
        idx_data += w.encode("utf-8") + b"\0" + struct.pack(">II", off, size)
    return dict_data, idx_data, locations


def build_idx_from_entries(entries: list[tuple[str, int, int]]) -> bytes:
    out = b""
    for w, off, size in entries:
        out += w.encode("utf-8") + b"\0" + struct.pack(">II", off, size)
    return out


def build_syn(entries: list[tuple[str, int]]) -> bytes:
    out = b""
    for w, ordinal in sorted(entries, key=lambda e: ascii_lower_key(e[0])):
        out += w.encode("utf-8") + b"\0" + struct.pack(">I", ordinal)
    return out


def deflate_chunks(data: bytes, chunk_len: int) -> list[bytes]:
    """One raw-deflate stream, full-flushed at every chunk boundary."""
    comp = zlib.compressobj(9, zlib.DEFLATED, -15)
    n = (len(data) + chunk_len - 1) // chunk_len
    chunks = []
    for i in range(n):
        piece = data[i * chunk_len : (i + 1) * chunk_len]
        out = comp.compress(piece)
        out += comp.flush(zlib.Z_FINISH if i == n - 1 else zlib.Z_FULL_FLUSH)
        chunks.append(out)
    return chunks


def dictzip_bytes(
    data: bytes,
    chunk_len: int,
    *,
    chunks: list[bytes] | None = None,
    lens: list[int] | None = None,
    chunk_count: int | None = None,
    chunk_len_field: int | None = None,
    version: int = 1,
    fname: bytes | None = None,
    fcomment: bytes | None = None,
    fhcrc: bool = False,
    pre_subfields: tuple[bytes, ...] = (),
    ra_repeat: int = 1,
    isize: int | None = None,
) -> bytes:
    if chunks is None:
        chunks = deflate_chunks(data, chunk_len)
    if lens is None:
        lens = [len(c) for c in chunks]
    declared_count = len(lens) if chunk_count is None else chunk_count
    declared_chunk_len = chunk_len if chunk_len_field is None else chunk_len_field

    ra = (
        b"RA"
        + struct.pack("<H", 6 + 2 * len(lens))
        + struct.pack("<HHH", version, declared_chunk_len, declared_count)
        + b"".join(struct.pack("<H", l) for l in lens)
    )
    extra = b"".join(pre_subfields) + ra * ra_repeat

    flg = 0x04  # FEXTRA
    if fname is not None:
        flg |= 0x08
    if fcomment is not None:
        flg |= 0x10
    if fhcrc:
        flg |= 0x02

    hdr = bytes([0x1F, 0x8B, 0x08, flg]) + b"\0\0\0\0" + bytes([0x02, 0x03])
    hdr += struct.pack("<H", len(extra)) + extra
    if fname is not None:
        hdr += fname + b"\0"
    if fcomment is not None:
        hdr += fcomment + b"\0"
    if fhcrc:
        hdr += struct.pack("<H", zlib.crc32(hdr) & 0xFFFF)

    trailer = struct.pack("<I", zlib.crc32(data) & 0xFFFFFFFF)
    trailer += struct.pack("<I", (len(data) if isize is None else isize) & 0xFFFFFFFF)
    return hdr + b"".join(chunks) + trailer


def verify_dictzip(blob: bytes, data: bytes, chunk_len: int) -> None:
    """Self-check a well-formed .dz: whole-stream gunzip + per-chunk random access."""
    assert gzip.decompress(blob) == data, "gunzip round-trip mismatch"

    # Reparse our own header to find the RA table and the data offset.
    flg = blob[3]
    assert flg & 0x04
    xlen = struct.unpack_from("<H", blob, 10)[0]
    pos = 12
    end = 12 + xlen
    lens: list[int] | None = None
    ra_chunk_len = None
    while pos + 4 <= end:
        si, sub_len = blob[pos : pos + 2], struct.unpack_from("<H", blob, pos + 2)[0]
        pos += 4
        if si == b"RA" and lens is None:
            ver, ra_chunk_len, count = struct.unpack_from("<HHH", blob, pos)
            assert ver == 1
            lens = list(struct.unpack_from(f"<{count}H", blob, pos + 6))
        pos += sub_len
    assert lens is not None and ra_chunk_len == chunk_len
    if flg & 0x08:
        pos = blob.index(b"\0", pos) + 1
    if flg & 0x10:
        pos = blob.index(b"\0", pos) + 1
    if flg & 0x02:
        pos += 2

    # Random access: each chunk must inflate independently to its payload slice.
    off = pos
    for i, clen in enumerate(lens):
        piece = zlib.decompressobj(-15).decompress(blob[off : off + clen])
        expected = data[i * chunk_len : (i + 1) * chunk_len]
        assert piece == expected, f"chunk {i} random-access mismatch"
        off += clen


def main() -> None:
    RESOURCES.mkdir(parents=True, exist_ok=True)
    written: list[str] = []

    def emit(name: str, blob: bytes) -> None:
        (RESOURCES / name).write_bytes(blob)
        written.append(f"{name} ({len(blob)} bytes)")

    # --- small: 11 entries, case-insensitively sorted, mixed-case first entry ---
    small_words = ["Apple", "banana", "box", "color", "dog", "love", "run", "stop", "story", "walk", "zebra"]
    small_defs = {w: definition_text(w) for w in small_words}
    small_dict, small_idx, small_loc = build_dict_and_idx(small_words, small_defs)
    emit("small.dict", small_dict)
    emit("small.idx", small_idx)

    SMALL_CHUNK = 32
    small_chunks = deflate_chunks(small_dict, SMALL_CHUNK)
    assert len(small_chunks) >= 5, "small payload must span several chunks"
    small_dz = dictzip_bytes(small_dict, SMALL_CHUNK, chunks=small_chunks)
    verify_dictzip(small_dz, small_dict, SMALL_CHUNK)
    emit("small.dict.dz", small_dz)

    # Sorted entry ordinals (0-based) for the .syn files.
    ordered = sorted(small_words, key=ascii_lower_key)
    ordinal = {w: i for i, w in enumerate(ordered)}
    # "dogs" maps to zebra on purpose: proves .syn precedence over the -s stemmer.
    emit(
        "small.syn",
        build_syn(
            [
                ("colour", ordinal["color"]),
                ("dogs", ordinal["zebra"]),
                ("pup", ordinal["dog"]),
                ("sprint", ordinal["run"]),
            ]
        ),
    )
    # Malformed .syn: valid entry, ordinal far past the entry count, then a
    # trailing entry truncated mid-ordinal ("zz" + 2 of 4 suffix bytes).
    evil_syn = build_syn([("colour", ordinal["color"]), ("ghost", 9999)])
    evil_syn += b"zz\0" + b"\xde\xad"
    emit("small_evil.syn", evil_syn)

    # --- malformed .idx variants ---
    sorted_entries = [(w, *small_loc[w]) for w in ordered]

    # Unsorted: swap banana (idx 1) and box (idx 2); offsets stay correct.
    unsorted = list(sorted_entries)
    unsorted[1], unsorted[2] = unsorted[2], unsorted[1]
    emit("small_unsorted.idx", build_idx_from_entries(unsorted))

    # Truncated: last entry (zebra) loses the final 4 bytes of its suffix.
    emit("small_truncated.idx", small_idx[:-4])

    # Evil offsets/sizes: dog points 1MB past EOF, love declares a ~4GB size,
    # run declares size 0. Everything else stays valid.
    evil_entries = []
    for w, off, size in sorted_entries:
        if w == "dog":
            evil_entries.append((w, 0x00100000, 16))
        elif w == "love":
            evil_entries.append((w, off, 0xFFFFFFF0))
        elif w == "run":
            evil_entries.append((w, off, 0))
        else:
            evil_entries.append((w, off, size))
    emit("small_evil.idx", build_idx_from_entries(evil_entries))

    # --- big: 701 entries so the .qidx sidecar gets multiple samples (256/512),
    # plus a 5004-byte definition that spans ~10 dictzip chunks ---
    big_words = ["boundary"] + [f"w{i:03d}" for i in range(700)]
    big_defs = {w: definition_text(w) for w in big_words}
    big_defs["boundary"] = boundary_definition()
    big_dict, big_idx, _ = build_dict_and_idx(big_words, big_defs)
    emit("big.dict", big_dict)
    emit("big.idx", big_idx)

    BIG_CHUNK = 512
    big_dz = dictzip_bytes(big_dict, BIG_CHUNK)
    verify_dictzip(big_dz, big_dict, BIG_CHUNK)
    emit("big.dict.dz", big_dz)

    # --- aligned: payload an exact multiple of the chunk length (4 * 32) ---
    aligned_data = bytes(range(64)) * 2
    assert len(aligned_data) == 128
    aligned_dz = dictzip_bytes(aligned_data, SMALL_CHUNK)
    verify_dictzip(aligned_dz, aligned_data, SMALL_CHUNK)
    emit("aligned.bin", aligned_data)
    emit("aligned.dz", aligned_dz)

    # --- happy-path header exercises: non-RA subfield before RA, FNAME,
    # FCOMMENT and a correct FHCRC ---
    fancy = dictzip_bytes(
        small_dict,
        SMALL_CHUNK,
        pre_subfields=(b"XX" + struct.pack("<H", 4) + b"\x01\x02\x03\x04",),
        fname=b"small.dict",
        fcomment=b"fixture with every optional header field",
        fhcrc=True,
    )
    verify_dictzip(fancy, small_dict, SMALL_CHUNK)
    emit("fancy.dz", fancy)

    # --- malformed .dz variants (payload: small.dict unless stated) ---

    # Plain gzip: no FEXTRA, no RA chunk table.
    emit("small_nora.dz", gzip.compress(small_dict, compresslevel=9, mtime=0))

    # RA declares one more chunk than the table holds (count vs table lie).
    emit("small_lying_count.dz", dictzip_bytes(small_dict, SMALL_CHUNK, chunk_count=len(small_chunks) + 1))

    # Chunk 0's compressed length understated to 4 bytes (remainder credited to
    # chunk 1): chunk 0 can't finish inflating, chunk 1 starts mid-stream.
    short_lens = [len(c) for c in small_chunks]
    moved = short_lens[0] - 4
    short_lens[0] = 4
    short_lens[1] += moved
    emit("small_short_len.dz", dictzip_bytes(small_dict, SMALL_CHUNK, chunks=small_chunks, lens=short_lens))

    # Every chunk length overstated to 0xFFFF: chunk 1+ offsets land far past EOF.
    emit(
        "small_len_overflow.dz",
        dictzip_bytes(small_dict, SMALL_CHUNK, chunks=small_chunks, lens=[0xFFFF] * len(small_chunks)),
    )

    # Chunk 3 replaced with same-length garbage whose first byte encodes deflate
    # BTYPE=11 (reserved -> immediate hard error); neighbours stay intact.
    garbage_chunks = list(small_chunks)
    garbage_chunks[3] = b"\x06" + b"\x00" * (len(small_chunks[3]) - 1)
    emit("small_garbage_chunk.dz", dictzip_bytes(small_dict, SMALL_CHUNK, chunks=garbage_chunks))

    # Declared chunkCount 16384 -- over the reader's MAX_CHUNK_COUNT (8192)
    # allocation cap -- with an internally CONSISTENT subfield: subLen covers
    # all 16384 LE16 lengths (6 + 32768 = 32774, fits uint16). Only the cap can
    # reject this one; a reader without the cap would reserve a 64KB chunk
    # table for a 473-byte file. (An inconsistent count-vs-subLen mismatch is
    # covered separately by small_lying_count.dz.)
    emit(
        "small_huge_count.dz",
        dictzip_bytes(small_dict, SMALL_CHUNK, chunks=small_chunks, lens=[10] * 16384, chunk_count=16384),
    )

    # Truncated inside the RA chunk table: header + subfield header + 6 RA
    # fixed bytes + first LE16 length + one stray byte.
    emit("small_trunc_table.dz", small_dz[: 12 + 4 + 6 + 2 + 1])

    # ISIZE trailer forced to 0.
    emit("small_zero_isize.dz", dictzip_bytes(small_dict, SMALL_CHUNK, isize=0))

    # Two RA subfields.
    emit("small_double_ra.dz", dictzip_bytes(small_dict, SMALL_CHUNK, ra_repeat=2))

    # RA chunkLength field 0.
    emit("small_zero_chlen.dz", dictzip_bytes(small_dict, SMALL_CHUNK, chunk_len_field=0))

    # RA version 2.
    emit("small_bad_ver.dz", dictzip_bytes(small_dict, SMALL_CHUNK, version=2))

    # Not gzip at all, and a zero-byte file.
    emit("not_gzip.bin", b"This is not a gzip stream at all, just plain text.\n" * 4)
    emit("empty.dz", b"")

    print(f"Wrote {len(written)} fixtures to {RESOURCES}:")
    for line in written:
        print(f"  {line}")


if __name__ == "__main__":
    sys.exit(main())
