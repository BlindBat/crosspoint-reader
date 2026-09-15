# Contract: SD-card fonts (.cpfont v4) and the font catalog

## Discovery and installation

Roots `/.fonts/` (preferred) and `/fonts/`; one directory per family; files `<name>_<size>.cpfont` (size 1..255; basename need not equal the folder). Names starting with `.` or `_` are skipped; duplicate sizes in a family are rejected; families are merged across roots (hidden wins on collision), sorted alphabetically, capped at 128. New families install under the family's existing root, else `/.fonts` when it exists, else `/fonts` when only it exists, else a newly created `/.fonts`. Family names and filenames are restricted to `[A-Za-z0-9_-]` (plus `.cpfont`), never containing `..`, `/` or `\`. Deleting the active family clears the selection and snaps the size back to the built-in set. Selectable family names must fit 31 bytes.

## File layout (little-endian)

Header (32 bytes): magic `CPFONT\0\0` (8), `u16 version` = 4, `u16 flags` (bit 0 = 2-bit bitmaps), `u8 styleCount` (1..4), 19 reserved. TOC (32 bytes × styleCount): `u8 styleId` (0 regular, 1 bold, 2 italic, 3 bold-italic), 3 pad, `u32 intervalCount`, `u32 glyphCount`, `u8 advanceY`, `s16 ascender`, `s16 descender`, `u16 kernLeftEntries`, `u16 kernRightEntries`, `u8 kernLeftClasses`, `u8 kernRightClasses`, `u8 ligaturePairs`, `u32 dataOffset`, 4 pad. Per-style data at `dataOffset`: intervals (`u32 first, last, offset`; strictly ascending, contiguous offsets), glyphs (16 bytes: `u8 w, u8 h, u16 advanceX (12.4), s16 left, s16 top, u16 dataLength, 2 pad, u32 dataOffset`), kern left/right class entries (`u16 cp, u8 class`), dense kern matrix (`int8` 4.4, L×R), ligature pairs (`u32 packed pair, u32 ligature`), 2-bit packed bitmaps. Caps: intervals ≤4096, glyphs ≤65536, kern entries ≤4096 per side; path ≤127 bytes. Font id = FNV-1a over the 32-byte header and the style TOC entries (not the glyph data), continued over the family name and point size (0 remapped to 1; an id already registered with the renderer is refused).

## Runtime behaviour

One reader-size file loaded (nearest installed size, ties to the smaller; the snapped size is persisted); additional 8/10/12 pt files loaded as UI fallbacks when the family covers U+4E00, U+3042, U+30A2 or U+AC00. Per-page glyph prewarm builds mini arenas (metadata-only for layout, bitmaps for render), retained while free heap ≥ 40 KB and released after three consecutive rebuilds using under ¾ of the arena; unions with resident glyphs unless over 512 glyphs or heap-tight; 8-slot overflow ring for on-demand glyphs; ≤768-entry advance table per style (≤4096 unique codepoints per build); per-page kern matrix restricted to the page's codepoints.

## Catalog (`fonts.json`, manifest version 1)

URL: `https://github.com/crosspoint-reader/crosspoint-fonts/releases/download/sd-fonts-m<FONTS_MANIFEST_VERSION>-b<CPFONT_VERSION>/fonts.json` (override with `-DFONT_MANIFEST_URL`). Shape: `{version:1, baseUrl, scriptGroups:[{tag,label}] (≤32 used), families:[{name, description, styles[], scripts[], files:[{name, size, crc32}]}]}`; a file without a numeric `crc32`, a group without tag/label, or another version aborts. Download: `baseUrl + file.name` per file (redirects may downgrade to plain HTTP), CRC32 (zlib-compatible) and `CPFONT` magic verified, the family directory deleted on abort, HTTP failure, CRC mismatch or bad magic; "update available" when any manifest file size differs from the installed file or is missing. Release CI tags immutable `sd-fonts-m<M>-b<B>-r<N>` releases and re-creates the stable `sd-fonts-m<M>-b<B>` release devices download from.

## Web API

See web-server.md (`/api/fonts`, `/api/fonts/upload`, `/api/fonts/delete`). Successful uploads and deletes mark the registry dirty so the next reader entry re-scans.
