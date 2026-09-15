# Contract: binary cache and progress formats

All POD fields are little-endian as written by `lib/Serialization/Serialization.h`; strings are `u32 length` + UTF-8 bytes. Readers reject a string length over 8192 or over the bytes remaining in the file before allocating. `docs/file-formats.md` holds ImHex patterns for `book.bin` and `section.bin`; the deltas below are authoritative where they differ.

## book.bin (version 10)

`u8 version(10)`, `u32 lutOffset`, `u16 spineCount`, `u16 tocCount`, Metadata{title, author, language, coverItemHref, textReferenceHref}, `u32 spineLut[spineCount]`, `u32 tocLut[tocCount]`, SpineEntry[]{href, u32 cumulativeSize, s16 tocIndex}, TocEntry[]{title, href, anchor, u8 level, s16 spineIndex}. Rejected (and rebuilt) when the version differs, the header or metadata is truncated, `lutOffset` ≠ position after metadata, a LUT exceeds the file, a spine entry is truncated, or the last TOC entry is unreadable. A short write deletes the file.

## sections/<n>.bin (version 45; partial sentinel 0xED; incomplete 0)

Header: `u8 version`, `s32 fontId`, `float lineCompression`, `bool extraParagraphSpacing`, `u8 paragraphAlignment`, `u16 viewportWidth`, `u16 viewportHeight`, `bool hyphenationEnabled`, `bool embeddedStyle`, `u8 imageRendering`, `bool focusReadingEnabled`, `u16 pageCount`, `u32 pageLutOffset`, `u32 anchorMapOffset`, `u32 paragraphLutOffset`, `u32 listItemLutOffset`, `u32 visibleTextLutOffset`.

Pages: `u16 elementCount`, elements tagged `u8` (1 PageLine{s16 x, s16 y, TextBlock}, 2 PageImage{s16 x, s16 y, ImageBlock{imagePath, srcPath, s16 w, s16 h}}, 3 PageHorizontalRule{s16 x, s16 y, u16 width, u8 thickness}), `u16 footnoteCount` + FootnoteEntry[]{char number[32], char href[256]}, `u16 linkCount` + PageLink[]{char href[256], s16 x, s16 y, s16 w, s16 h} (v44+).

TextBlock: `u16 wordCount`, `u8 hasFocus`, `u16 textBytes`, `u16 textOff[wordCount]`, `s16 wordXPos[wordCount]`, `u16 wordFocusSuffixX[wordCount]` (if hasFocus), `u8 wordStyle[wordCount]` (bits: BOLD 1, ITALIC 2, UNDERLINE 4, STRIKETHROUGH 8, SUP 16, SUB 32, RUBY_CONTINUE 64), `u8 wordFocusBoundary[wordCount]` (if hasFocus), `char text[textBytes]` (NUL-separated), then `wordCount` ruby strings, then BlockStyle{u8 alignment, bool textAlignDefined, s16 marginTop/Bottom/Left/Right, s16 paddingTop/Bottom/Left/Right, s16 textIndent, bool textIndentDefined, bool isRtl, bool directionDefined}. Limits: ≤10000 words, ≤64 KB text; offsets must start at 0, increase strictly, stay in bounds and be NUL-terminated.

Tables: `u32 pageLut[pageCount]`; anchor map `u16 count` + {string anchor, u16 page}[]; paragraph LUT `u16 count` + `u16 paragraphIndex[]`; list-item LUT `u16 listItemIndex[count]`; visible-text LUT `u32 offset[pageCount]`; partial files append `u32 bytesConsumed`, `u32 totalBytes`.

Rejected and deleted: unknown version, spec mismatch, truncated header, non-monotonic or out-of-file table offsets, a partial whose trailer does not fit. A page whose LUT entry or data lies outside the file yields no page.

## css_rules.cache (version 12)

`u8 version`, `u8 flags` (bit 0 partial), `u16 ruleCount`, per rule `u16 selectorLen` + lowercase selector bytes + a 62-byte style record (5 enum bytes, 11 × {float, u8 unit}, display, verticalAlign, u32 definedBits). Replaced atomically via `.tmp`/`.bak`; a partial cache is retried on later loads; any content change deletes all section caches.

## Image caches

`img_<spine>_<n>.<ext>`: the original bytes extracted from the EPUB on first render. `.pxc`: `u16 width`, `u16 height`, then `((width+3)/4)` bytes per row of 2-bit pixels MSB-first (0 black … 3 white). Invalid when the size differs from the expected by more than 1 px or the file is shorter than `4 + rowBytes×height`.

## Covers and thumbnails

Top-down BMP, 72 DPI: 1-bit = 62-byte header (14 + 40 + 2-entry palette black/white), rows padded to 4 bytes; 2-bit = 70-byte header (4-entry palette 0x00, 0x55, 0xAA, 0xFF). `thumb_<h>.bmp` is 1-bit at 0.6h × h; an empty thumb file marks "no cover" so the home screen does not retry.

## Progress files

- EPUB `progress.bin`: `u16 spine`, `u16 page` (0xFFFF stale sentinel), `u16 pageCount`, optional `u32 visibleTextOffset` (6 or 10 bytes; 4 accepted).
- TXT `progress.bin`: `u16 page`, two zero bytes.
- XTC `progress.bin`: `u32 page index`.
- FB2 `progress.bin`: `u16 section`, `u16 page`, `u16 pageCount` (4 accepted).
All written via `progress.bin.tmp` → remove old → rename.

## TXT index.bin (version 3)

`u32 magic 'TXTI' (0x54585449)`, `u8 version`, `u32 fileSize`, `s32 viewportWidth`, `s32 linesPerPage`, `s32 fontId`, `s32 screenMargin`, `u8 paragraphAlignment`, `u32 numPages`, `u32 pageStart[numPages]`. Any mismatch with the current file size or settings rebuilds it.

## FB2 caches (fork-only)

`book.bin` v2: `u8 version`, strings title/author/language/coverBinaryId (≤4096 on read), `u16 sectionCount` + {title, u32 fileOffset, u32 length}[], `u16 tocCount` + {title, s16 sectionIndex}[]; counts are checked against the remaining file size before `reserve()`. `sections/<n>.bin` v4: 23-byte header (`u8 version`, `s32 fontId`, `float lineCompression`, `bool extraParagraphSpacing`, `u8 paragraphAlignment`, `u16 viewportWidth`, `u16 viewportHeight`, `bool hyphenationEnabled`, `bool focusReadingEnabled`, `u16 pageCount`, `u32 lutOffset`), Page records, `u32 pageOffset[pageCount]`.

## Bookmarks JSON

`{"bookmarks":[{"xpath":str,"percentage":float,"summary":str,"si":int,"pc":int,"pp":float,"vo":int?}]}`, newest first.

## Dictionary sidecars

`.qidx` / `.sidx`: six native-endian `u32` header words {magic `QIDX` 0x58444951 / `SIDX` 0x58444953, version 2, sampleInterval 256, sampleCount, sourceFileSize, entryCount} followed by `sampleCount` × `u32` byte offsets (entry 0 and every 256th entry). The header is zero until the build completes; a mismatch in magic, version, interval or source size marks it stale.
