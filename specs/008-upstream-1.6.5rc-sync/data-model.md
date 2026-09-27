# Data Model: Upstream 1.6.5rc Sync

**Date**: 2026-09-27 | **Spec**: [spec.md](spec.md) | **Research**: [research.md](research.md)

The sync adds no new persistent format of its own. It adopts upstream's Library index and changes
what may appear in it, moves the fork's release/version identifiers to a new scheme, and shifts
one cache version. Everything below is the state *after* the sync.

## Library index entry (upstream CLX1 — now valid for FB2)

Source of truth: `lib/LibraryIndex/LibraryFormat.h` at `1.6.5rc` (128-byte `ClixRecord`, 512-byte
aligned sections, `CLIX_FORMAT_VERSION` 2, `CLIX_FOLD_VERSION` 3, `CLIX_MAX_RECORDS` 4,096).

| Field | Meaning | FB2 note |
|---|---|---|
| `nameOff`, `nameLen` | filename in the name blob | unchanged |
| path hash (blob prefix) | FNV-1a 64 of the full path | identifies the same book for Recent lookup; unchanged |
| `fileSize`, `modificationTime` | identity + freshness | unchanged |
| `folderId`, `firstSeen` | folder ordinal, arrival order | unchanged |
| `fold[96]`, `foldLen` | folded title for the Title tab | folded from the FB2 title when metadata is on, else the filename stem |
| `authorKey[12]`, `authorKeyLen` | author sort key | from the FB2 author (first/middle/last as the fork already composes them), cleaned like EPUB authors |
| `metadataStatus` | `NOT_ATTEMPTED` / `EXTRACTED` / `FAILED` | now set for `.fb2` exactly as for `.epub`; `.txt/.md/.xtc` stay `NOT_ATTEMPTED` |

**Validation rules** (unchanged, upstream's): header magic/version/fold-version, `selfSize` equals
the real file size, counts within range, sections consistent. A rejected index is rebuilt.

**Discovery rule** (changed): `isBookName()` accepts `.epub`, `.txt`, `.md`, `.xtc` **and `.fb2`**.

**Metadata rule** (changed): with **Use book metadata** on, extraction is attempted for `.epub`
and `.fb2`; a prior record is reused when size, mtime, fold version, metadata setting and status
all match (upstream's rule, unchanged).

## FB2 metadata source

Source: `lib/Fb2/Fb2/Fb2MetadataParser.{h,cpp}` (fork). Fields read from `<description>/<title-info>`:
`book-title` → title; `author/{first-name,middle-name,last-name}` → author; `lang`; `coverpage/image`
→ cover binary id. For the index only title and author are needed; both are NFC-composed
(`utf8ComposeNfc`) before use, matching upstream's EPUB path.

**States of a read** (`Fb2::loadMetadata`):

```text
book.bin v5 present & valid ──► title/author from the cache header (no parse)
book.bin absent/invalid     ──► metadata-only parse, stops at </title-info> ──► success
                                                                              └─► parse error → false → row uses filename stem, status FAILED
```

The metadata-only parse never writes `book.bin`, never touches `sections/`, never reads `<body>`.

## Book caches

| Cache | Constant | Before | After | Effect on a card from `1.6.0-bb.5` |
|---|---|---|---|---|
| EPUB `book.bin` | `BOOK_CACHE_VERSION` | 10 | 10 | loads |
| EPUB `sections/<n>.bin` | `SECTION_FILE_VERSION` | 45 | **46** (upstream) | rebuilt once per chapter opened |
| CSS cache | `CSS_CACHE_VERSION` | 12 | 12 | loads |
| FB2 `book.bin` | `FB2_CACHE_VERSION` | 5 | 5 | loads |
| FB2 sections | `FB2_SECTION_FILE_VERSION` | 5 | 5 | loads (page layout unchanged in v46) |
| TXT page index | `CACHE_VERSION` | 3 | 3 | loads |
| Cover BMPs | file name | `cover.bmp` (FB2) | `cover_original.bmp` / `cover_legacy_v2.bmp` (FB2, like EPUB) | regenerated once; old file orphaned |
| Library index | `CLIX_FORMAT_VERSION` | — | 2 | built on first Library open |

## Release identifiers

| Item | Scheme | This sync |
|---|---|---|
| Configured version (`platformio.ini`) | `<upstream>rc-bb.N` on an upstream RC; `<upstream>-bb.N` on an upstream final | `1.6.5rc-bb.1` |
| Git tag | equals the configured version, no `v` | `1.6.5rc-bb.1` |
| GitHub release flag | pre-release for RC-based; normal otherwise | pre-release |
| Asset names | `crosspoint-<tag>-<device>.bin`, devices `x3-x4`, `sticky`, `x4pro`, `x4c`, `papermono` | `crosspoint-1.6.5rc-bb.1-x3-x4.bin` … |
| Firmware version string | release env: the version; RC env: `<version>-rc+<sha>` | `1.6.5rc-bb.1-rc+<sha>` (contains the tag) |

## Host test program

| | Before | After |
|---|---|---|
| Suites | 59 | 69 (59 − `sdcard_font` + 11 upstream) |
| Tests | 3,349 | ≥ 3,349 − SmartSync cases + 132 + new FB2 cases (measured at the end) |
| Modes | plain, ASan+UBSan | same |
| Registry | `crosspoint_suite()` list in `test/CMakeLists.txt` | same, extended |

## Fork feature inventory (kept — spec inventory B)

Each row maps to the host suites that pin it, which is how "kept" is proven:

| Feature | Pinned by |
|---|---|
| FB2 support (specs/003–007) | `fb2`, `fb2_book`, `fb2_common`, `fb2_cover_extractor`, `fb2_filename`, `fb2_metadata_parser`, `fb2_section_cache`, `fb2_section_parser`, `xtc_fb2_readers`, `reader_helpers` |
| Stub cover sleep screen (specs/002) | `image_common`, `gfx_renderer`, `reader_helpers` (sleep image helpers) |
| Hardening | `alloc_guards`, `zip_file`, `book_metadata_cache`, `epub_section`, `txt_reader`, `serialization`, `png_decode`, `jpeg_to_bmp`, `web_dav_paths`, `network_helpers`, `persistable_stores`, corpus suites |
| Performance seams | `alloc_guards`, `text_block_layout`, `platform_helpers` |
| UX fixes | `reader_helpers`, `library_helpers`, `font_system`, `settings_input` |
