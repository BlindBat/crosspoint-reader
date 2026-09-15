# Contract: XTC / XTCH container

Little-endian. Header at offset 0 (56 bytes; a 48-byte legacy layout whose page table starts at 0x30 is accepted and treated as chapterless):

| Offset | Field |
|--------|-------|
| 0x00 | `u32 magic`: `XTC\0` (0x00435458, 1-bit) or `XTCH` (0x48435458, 2-bit) |
| 0x04 | `u8 versionMajor`, `u8 versionMinor` — accepted 1.0 or byte-swapped 0.1 |
| 0x06 | `u16 pageCount` (> 0) |
| 0x08 | `u8 readDirection`, `u8 hasMetadata`, `u8 hasThumbnails`, `u8 hasChapters` |
| 0x0C | `u32 currentPage` (ignored) |
| 0x10 | `u64 metadataOffset` (ignored) |
| 0x18 | `u64 pageTableOffset` (≥ 0x30 and ≤ file size; `pageCount × 16` must fit) |
| 0x20 | `u64 dataOffset`, 0x28 `u64 thumbOffset` (ignored) |
| 0x30 | `u32 chapterOffset` (read as exactly 4 bytes), `u32 padding` |

Metadata when `hasMetadata`: title NUL-terminated at 0x38 (127 bytes read), author at 0xB8 (63 bytes). Empty title → file name without extension.

Page table: `pageCount` × {`u64 dataOffset`, `u32 dataSize`, `u16 width`, `u16 height`}, read one entry at a time on demand.

Page data: 22-byte header {`u32 magic` `XTG` (0x00475458) or `XTH` (0x00485458), `u16 width`, `u16 height`, `u8 colorMode`, `u8 compression`, `u32 dataSize`, `u64 md5`} followed by the bitmap: XTG = `((width+7)/8) × height` bytes row-major MSB-first (0 black, 1 white); XTH = two planes of `((width×height+7)/8)` bytes, column-major right-to-left, 8 vertical pixels per byte MSB = top, value = (bit1 << 1) | bit2 with 0 white, 1 dark grey, 2 light grey, 3 black. The page magic must match the container's bit depth; a caller buffer smaller than the implied bitmap is refused; short data is a read error.

Chapters when `hasChapters == 1` and `chapterOffset ∈ [56, fileSize − 96]`: consecutive 96-byte records {`char name[80]`, `u16 startPage` at 0x50 (1-based), `u16 endPage` at 0x52} up to the page table or data offset, terminated by an all-zero record; records whose start is past the last page or after their end are dropped; ends are clamped. Parsed lazily on first request; the file is closed between reads.

Reader behaviour: portrait only; pages blitted from the top-left at native pixels; 2-bit pages use a four-pass grayscale sequence; status-bar overlay (Hide/Bottom/Top) on 1-bit pages only; progress = `u32` page index; cover = page 0 (2-bit palette BMP for XTCH, 1-bit for XTC); thumbnails 1-bit, area-averaged, never upscaled.
