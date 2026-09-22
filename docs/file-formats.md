# File Formats

These formats describe the SD-card files CrossPoint writes under `/.crosspoint/`.
All POD fields are written in the ESP32 little-endian representation used by
`Serialization.h`; strings are length-prefixed UTF-8 (a `u32` byte length followed
by that many bytes, with no NUL terminator). Readers reject a string whose length
prefix exceeds 8192 bytes or the bytes remaining in the file before allocating.

## Directory layout

Every book gets one cache directory named `<prefix>_<hash>`, where `<hash>` is
`std::to_string(std::hash<std::string>{}(filepath))` over the book's full path.
Moving or renaming a book therefore produces a new directory and loses its cache
and progress.

| Prefix | Written by | Contents |
| --- | --- | --- |
| `epub_<hash>` | `Epub` | `book.bin`, `css_rules.cache`, `html/`, `sections/`, `img_*`, covers, `progress.bin` |
| `fb2_<hash>` | `Fb2` | `book.bin`, `sections/`, covers, `progress.bin` |
| `txt_<hash>` | `Txt` | `index.bin`, `cover.bmp`, `progress.bin` |
| `xtc_<hash>` | `Xtc` | covers, thumbnails, `progress.bin` |

The EPUB cache directory holds:

| Path | Purpose |
| --- | --- |
| `book.bin` | Metadata plus spine/TOC lookup tables |
| `spine.bin.tmp`, `toc.bin.tmp` | Build scratch files, removed by `cleanupTmpFiles()` |
| `css_rules.cache` (`.tmp`, `.bak`) | Flattened stylesheet rules |
| `html/<n>.html` | Inflated chapter HTML, keyed on the book only |
| `html/.tmp_<n>.html` | Inflate scratch, promoted by rename |
| `sections/<n>.bin` | Laid-out pages for spine item `n` |
| `sections/<n>.bin.part` | Open build file, renamed over `<n>.bin` on commit |
| `img_<spine>_<n><ext>` | Image bytes extracted from the EPUB |
| `img_<spine>_<n>.pxc` | Decoded 2-bit pixel cache for that image |
| `cover.bmp`, `cover_crop.bmp` | Fit and cropped cover renderings |
| `thumb_<height>.bmp` | Home-screen thumbnail |
| `.cover.jpg`, `.cover.png`, `.tmp.css` | Extraction scratch, removed after use |
| `progress.bin` (`.tmp`) | Reading position |

JSON stores that are not per-book live directly in `/.crosspoint/` (see
[JSON stores](#json-stores)).

## `book.bin`

### Version 10

`book.bin` stores EPUB metadata plus lookup tables for spine and TOC entries.
The current firmware writes this version from `BookMetadataCache`.

The build streams spine and TOC entries into `spine.bin.tmp` and `toc.bin.tmp`
first, then merges them into `book.bin` so the lookup tables can be written
ahead of the entries they point at. A short write deletes `book.bin` rather than
leaving a truncated file that would still pass the version check.

`load()` rejects (and so rebuilds) the cache when the version differs, the header
or metadata is truncated, `lutOffset` is not exactly the position after the
metadata block, either lookup table would run past the end of the file, a spine
entry is truncated, or the last TOC entry cannot be read through its LUT slot.

ImHex pattern:

```c++
import std.mem;
import std.string;
import std.core;

#define EXPECTED_VERSION 10
#define MAX_STRING_LENGTH 65535

struct String {
    u32 length [[hidden, comment("String byte length")]];
    if (length > MAX_STRING_LENGTH) {
        std::warning(std::format("Unusually large string length: {} bytes", length));
    }
    char data[length] [[comment("UTF-8 string data")]];
} [[sealed, format("format_string"), comment("Length-prefixed UTF-8 string")]];

fn format_string(String s) {
    return s.data;
};

struct Metadata {
    String title [[comment("Book title")]];
    String author [[comment("Book author")]];
    String language [[comment("Book language code")]];
    String coverItemHref [[comment("Path to cover image")]];
    String textReferenceHref [[comment("Path to guided first text reference")]];
};

struct SpineEntry {
    String href [[comment("Resource path")]];
    u32 cumulativeSize [[comment("Cumulative uncompressed spine size through this entry")]];
    s16 tocIndex [[comment("Index into TOC, or inherited/previous TOC index when no direct entry exists")]];
};

struct TocEntry {
    String title [[comment("Chapter/section title")]];
    String href [[comment("Resource path")]];
    String anchor [[comment("Fragment identifier")]];
    u8 level [[comment("Nesting level")]];
    s16 spineIndex [[comment("Index into spine (-1 if none)")]];
};

struct BookBin {
    u8 version;
    if (version != EXPECTED_VERSION) {
        std::error(std::format("Unsupported version: {} (expected {})", version, EXPECTED_VERSION));
    }

    u32 lutOffset [[comment("Offset to lookup tables")]];
    u16 spineCount;
    u16 tocCount;

    Metadata metadata;

    u32 currentOffset = $;
    if (currentOffset != lutOffset) {
        std::warning(std::format("LUT offset mismatch: expected 0x{:X}, got 0x{:X}", lutOffset, currentOffset));
    }

    u32 spineLut[spineCount] [[comment("Spine entry offsets")]];
    u32 tocLut[tocCount] [[comment("TOC entry offsets")]];

    SpineEntry spines[spineCount];
    TocEntry toc[tocCount];
};

BookBin book @ 0x00;

u32 fileSize = std::mem::size();
u32 parsedSize = $;
if (parsedSize != fileSize) {
    std::warning(std::format("Unparsed data detected: {} bytes remaining at offset 0x{:X}", fileSize - parsedSize, parsedSize));
}
```

## `section.bin`

### Version 45

Each file in `sections/*.bin` stores one laid-out spine section. The 41-byte
header is also the cache-busting key: if any layout-affecting setting differs
from the current reader settings, the section is discarded and rebuilt.

Version 45 keeps the version 44 serialized layout unchanged. It was bumped
because internal EPUB links now preserve CSS superscript and subscript styles,
changing their cached word-style flags and page layout.

#### Version byte states

The version byte is written last so it is the commit point, and it carries three
distinct meanings:

| Value | Meaning |
| --- | --- |
| `45` | Finalized section: `pageCount` is the chapter's true page count |
| `0xED` | Partial section suspended mid-build; carries a watermark trailer |
| `0` | Incomplete build (crash or power loss); rejected and deleted on load |

The partial sentinel is derived as `0xFE - (SECTION_FILE_VERSION - 28)`, so it
changes in lockstep with the format version and a stale-format partial can never
pass the header check.

A build writes into `sections/<n>.bin.part` and renames it over `sections/<n>.bin`
only after the tables are written and the header is patched, so an interrupted
build never replaces a valid file.

#### Partial sections

A build that is suspended (reader exited, device slept) commits the pages it has
already laid out under the `0xED` sentinel, together with every lookup table and
a trailer appended immediately after the visible-text LUT:

```c++
u32 bytesConsumed;  // HTML bytes the parser had consumed at suspend
u32 totalBytes;     // total HTML bytes in the chapter
```

The reader extrapolates the chapter's total page count from that ratio and keeps
rebuilding in the background. The incomplete trailing page is deliberately not
flushed, and anchors that landed on it are skipped, so a partial only ever holds
fully laid-out pages. A finalized file is validated further: the table offsets
must be monotonically ordered and the visible-text LUT must fit inside the file.

#### Version history

Version 44 appends the internal-link rectangles produced during text layout to
each serialized page. The reader uses these rectangles for touch navigation;
older caches are rebuilt because they contain no link geometry.

Version 43 keeps the version 42 serialized layout unchanged. It was bumped
because paragraph base direction now excludes direction changes from inline
elements.

Version 42 keeps the version 41 serialized layout unchanged. It was bumped
because closing a block now strips inherited vertical margins and padding.

Version 41 keeps the version 40 serialized layout unchanged. It was bumped
because simple HTML table rows are now laid out as positioned columns rather
than flattened paragraphs with synthetic row/cell labels.

Version 40 keeps the version 39 serialized layout unchanged. It was bumped
because ruby groups now remain intact when large text blocks are soft-flushed.

Version 39 keeps the version 38 serialized layout unchanged. It was bumped
because image top margins are now clamped to keep full-height images within the
page viewport.

Version 38 keeps the version 37 serialized layout unchanged. It was bumped
because Focus Reading now permits line breaks at visible hyphens and dashes
and hyphenates focus-split words as a whole, changing cached page layout.

Version 37 increases the fixed-size footnote href field from 96 to 256 bytes.
This changes each serialized footnote record from 128 to 288 bytes, so older
section caches must be discarded and rebuilt.

Version 36 keeps the version 35 serialized layout unchanged. It was bumped
because ruby and justified text positioning and CJK line breaking now use
corrected word measurements, so version 35 cached page layouts no longer match.

Version 35 adds a header offset and a `uint32_t` entry per page for the
visible-text offset LUT. The other section LUTs remain unchanged.

Version 34 is binary-identical to version 33. The version was bumped because
word-gap suppression was narrowed to tokens glued together in the source: v33
dropped the gap between any two words meeting at a CJK break opportunity, which
collapsed the spaces between Hangul words, so v33 word positions no longer match
what the layout engine now produces. Ruby element boundaries carry the
continuation flag instead. The same version also changed `<br>` handling: a
`<br>` after text is now a margin-stripped line break, and only a `<br>` whose
block stays empty injects the scene-break gap.

Version 33 added `<ruby>` / `<rt>` support (and skips `<rp>`), which is what the
per-word ruby strings and the `RUBY_CONTINUE` style bit serialize.

Version 32 appends the book-internal source href to each `ImageBlock` after the
cache path, so images can be header-probed at build time and extracted lazily on
first render.

Version 30 is binary-identical to version 29. The version was bumped because
Arabic contextual shaping changed text measurement (`getTextAdvanceX` now
measures the shaped visual text), so word positions cached by v29 no longer
match what `drawText` renders.

Version 29 replaced v28's length-prefixed word strings and per-field arrays with
flat TextBlock word storage: per-word arrays plus one shared NUL-terminated text
blob. The on-disk order mirrors the in-RAM arena so the firmware reads a whole
block payload with a single allocation and a single SD read.

Version 28 added line-through to the serialized word-style bits. The format also
includes:

- cache-busting fields for paragraph alignment, hyphenation, embedded CSS,
  image rendering mode, and Focus Reading
- page offset LUT
- per-page visible-text offset LUT (zero-based Unicode codepoints in `<body>`)
- anchor-to-page map for fragment and footnote navigation
- paragraph and list-item LUTs retained for navigation and legacy sync fallback
- optional per-word Focus Reading split metadata
- per-page footnote entries

#### Per-page links and ruby

Each page ends with up to `MAX_LINKS_PER_PAGE` (32) fixed-size link records
appended after the footnote records. Each record is a 256-byte href buffer
followed by four `s16` fields giving a rectangle relative to the page origin; the
reader adds its oriented margins when hit-testing. A record whose href is empty
or whose width or height is not positive fails the page load.

`TextBlock` serializes one length-prefixed ruby string per word, immediately
after the word arena and before the block style — words with no annotation write
a zero-length string. Because the entries are positional, every block pays one
`u32` per word on disk whether or not it carries ruby. Deserialization only
materializes the vector once a non-empty annotation appears, so books without
ruby pay no RAM for it.

Ruby grouping is carried in the word style byte: `RUBY_CONTINUE` (bit 6, value
64) marks a word as a follower of the preceding group leader. A group leader is a
word with a non-empty ruby string and the flag clear; the group extends over the
following words while the flag stays set. `getFont()` ignores every bit above
bit 1, so the flag composes freely with bold/italic and the decoration bits.

Note that `TextBlock::LinkSpan` is layout-only metadata: the parser moves it into
the page's link list before the block is serialized, so it never reaches disk.

ImHex pattern:

```c++
import std.mem;
import std.string;
import std.core;

#define EXPECTED_VERSION 45
#define PARTIAL_VERSION 0xED
#define MAX_STRING_LENGTH 65535
#define FOOTNOTE_NUMBER_LEN 32
#define FOOTNOTE_HREF_LEN 256

struct String {
    u32 length [[hidden, comment("String byte length")]];
    if (length > MAX_STRING_LENGTH) {
        std::warning(std::format("Unusually large string length: {} bytes", length));
    }
    char data[length] [[comment("UTF-8 string data")]];
} [[sealed, format("format_string"), comment("Length-prefixed UTF-8 string")]];

fn format_string(String s) {
    return s.data;
};

enum PageElementTag : u8 {
    TAG_PageLine = 1,
    TAG_PageImage = 2,
    TAG_PageHorizontalRule = 3
};

enum WordStyle : u8 {
    REGULAR = 0,
    BOLD = 1,
    ITALIC = 2,
    BOLD_ITALIC = 3,
    UNDERLINE = 4,
    STRIKETHROUGH = 8,
    SUP = 16,
    SUB = 32,
    RUBY_CONTINUE = 64
};

enum TextAlign : u8 {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
    NONE = 4
};

struct BlockStyle {
    TextAlign alignment;
    bool textAlignDefined;
    s16 marginTop;
    s16 marginBottom;
    s16 marginLeft;
    s16 marginRight;
    s16 paddingTop;
    s16 paddingBottom;
    s16 paddingLeft;
    s16 paddingRight;
    s16 textIndent;
    bool textIndentDefined;
    bool isRtl;
    bool directionDefined;
};

struct TextBlock {
    u16 wordCount;
    u8 hasFocus;
    u16 textBytes [[comment("Total size of text[], including one NUL per word")]];

    if (wordCount > 0) {
        u16 textOff[wordCount] [[comment("Byte offset of word i's text within text[]")]];
        s16 wordXPos[wordCount];
        if (hasFocus != 0) {
            u16 wordFocusSuffixX[wordCount] [[comment("Suffix x offset from word start")]];
        }
        WordStyle wordStyle[wordCount];
        if (hasFocus != 0) {
            u8 wordFocusBoundary[wordCount] [[comment("UTF-8 byte boundary between bold prefix and suffix")]];
        }
        char text[textBytes] [[comment("All words back to back, each NUL-terminated")]];
        String ruby[wordCount] [[comment("Ruby annotation per word; empty when the word has none")]];
    }

    BlockStyle blockStyle;
};

struct ImageBlock {
    String imagePath [[comment("Cached image path under the book cache dir")]];
    String srcPath [[comment("Book-internal source href; empty once known-extracted")]];
    s16 width;
    s16 height;
};

struct PageLine {
    s16 xPos;
    s16 yPos;
    TextBlock block;
};

struct PageImage {
    s16 xPos;
    s16 yPos;
    ImageBlock image;
};

struct PageHorizontalRule {
    s16 xPos;
    s16 yPos;
    u16 width;
    u8 thickness;
};

struct PageElement {
    PageElementTag pageElementType;
    if (pageElementType == TAG_PageLine) {
        PageLine pageLine [[inline]];
    } else if (pageElementType == TAG_PageImage) {
        PageImage pageImage [[inline]];
    } else if (pageElementType == TAG_PageHorizontalRule) {
        PageHorizontalRule horizontalRule [[inline]];
    } else {
        std::error(std::format("Unknown page element type: {}", pageElementType));
    }
};

struct FootnoteEntry {
    char number[FOOTNOTE_NUMBER_LEN];
    char href[FOOTNOTE_HREF_LEN];
};

struct PageLink {
    char href[FOOTNOTE_HREF_LEN];
    s16 x [[comment("Page-relative rectangle; reader adds oriented margins")]];
    s16 y;
    s16 width;
    s16 height;
};

struct Page {
    u16 elementCount;
    PageElement elements[elementCount] [[inline]];

    u16 footnoteCount [[comment("At most 16")]];
    FootnoteEntry footnotes[footnoteCount];

    u16 linkCount [[comment("At most 32; v44+")]];
    PageLink links[linkCount];
};

struct AnchorEntry {
    String anchor;
    u16 page;
};

struct AnchorMap {
    u16 count;
    AnchorEntry entries[count];
};

struct ParagraphLut {
    u16 count;
    u16 paragraphIndex[count];
};

struct PartialTrailer {
    u32 bytesConsumed [[comment("HTML bytes consumed when the build was suspended")]];
    u32 totalBytes [[comment("Total HTML bytes in the chapter")]];
};

struct SectionBin {
    u8 version;
    if (version != EXPECTED_VERSION && version != PARTIAL_VERSION) {
        std::error(std::format("Unsupported version: {} (expected {} or partial {})", version, EXPECTED_VERSION, PARTIAL_VERSION));
    }

    s32 fontId;
    float lineCompression;
    bool extraParagraphSpacing;
    u8 paragraphAlignment;
    u16 viewportWidth;
    u16 viewportHeight;
    bool hyphenationEnabled;
    bool embeddedStyle;
    u8 imageRendering [[comment("0 display, 1 alt-text placeholder, 2 suppress")]];
    bool focusReadingEnabled;

    u16 pageCount;
    u32 pageLutOffset;
    u32 anchorMapOffset;
    u32 paragraphLutOffset;
    u32 listItemLutOffset;
    u32 visibleTextLutOffset;

    Page pages[pageCount];

    u32 currentOffset = $;
    if (currentOffset != pageLutOffset) {
        std::warning(std::format("Page LUT offset mismatch: expected 0x{:X}, got 0x{:X}", pageLutOffset, currentOffset));
    }

    u32 pageLut[pageCount] [[comment("Page data offsets")]];

    if (anchorMapOffset != 0) {
        AnchorMap anchorMap @ anchorMapOffset;
    }

    if (paragraphLutOffset != 0) {
        ParagraphLut paragraphLut @ paragraphLutOffset;
    }

    if (listItemLutOffset != 0 && paragraphLutOffset != 0) {
        u16 listItemIndex[paragraphLut.count] @ listItemLutOffset;
    }

    if (visibleTextLutOffset != 0) {
	u32 visibleTextOffset[pageCount] @ visibleTextLutOffset;
    }

    if (version == PARTIAL_VERSION && visibleTextLutOffset != 0) {
        PartialTrailer partialTrailer @ (visibleTextLutOffset + pageCount * 4);
    }
};

SectionBin section @ 0x00;

u32 fileSize = std::mem::size();
u32 parsedSize = $;
if (parsedSize != fileSize) {
    std::warning(std::format("Unparsed data detected: {} bytes remaining at offset 0x{:X}", fileSize - parsedSize, parsedSize));
}
```

#### TextBlock validation limits

Deserialization rejects a block (and so the whole page) when the word count
exceeds 10000, when `textBytes` is non-zero for zero words, or when `textBytes`
is smaller than the word count (every word carries at least its NUL). It then
verifies that `textOff[0]` is 0, that offsets increase strictly and stay below
`textBytes`, and that every word — including the last — is NUL-terminated.

## `css_rules.cache`

### Version 12

`css_rules.cache` holds the flattened selector/style pairs parsed from the book's
stylesheets, so a rebuild after a settings change skips CSS parsing entirely.

```c++
u8  version;       // 12
u8  flags;         // bit 0 = partial (not every source rule was captured)
u16 ruleCount;     // at most 1500
// per rule:
u16 selectorLen;   // 1..256
char selector[selectorLen];
u8  style[66];
```

The 66-byte style record is written by `encodeStyleWire`, in this order: five
enum bytes (`textAlign`, `fontStyle`, `fontWeight`, `textDecoration`,
`direction`), then eleven `CssLength` fields as `float value` + `u8 unit`
(`textIndent`, `marginTop/Bottom/Left/Right`, `paddingTop/Bottom/Left/Right`,
`imageHeight`, `imageWidth`), then `display` and `verticalAlign`, then a `u32`
mask of 18 "defined" bits in that same field order.

A cache is written to `css_rules.cache.tmp`; any existing file is moved to
`css_rules.cache.bak` before the temp is promoted, and the backup is restored if
the promotion fails. A load that finds no final file but a leftover backup
restores it. The partial flag is set when a stylesheet was skipped (free heap
below the parsing threshold, or a source file over the size limit), when the rule
or selector pools stopped growing, or when the input ended mid-construct. It
means the cache can style the current session but must be re-derived from the
source stylesheets on the next book load; the total selector text is also capped
at 32 KB.

## `html/`

`html/<n>.html` is the inflated XHTML of spine item `n`. It is keyed on the book
only — it lives in the per-book cache directory and does not depend on the render
settings — so it survives the invalidation that wipes the `sections/*.bin` layout
caches when the font, margin or orientation changes, and a re-pagination then
skips zip inflation entirely.

Inflation streams into `html/.tmp_<n>.html` in 8 KB chunks (up to three attempts,
50 ms apart) and the temp is promoted by rename as soon as the inflate
succeeds, before layout starts. A spine item whose `.bin` never finalizes
therefore still leaves usable cached HTML behind. If the file already exists it
is treated as complete and reused.

## Image artefacts

`img_<spineIndex>_<n><ext>` holds the bytes of image `n` of spine item
`spineIndex`, extracted verbatim from the EPUB, with the extension taken from the
source href. During layout only the image header is probed for its dimensions;
extraction is deferred to the first render, unless the probe fails, in which case
the whole image is extracted at parse time behind the indexing popup.

`img_<spineIndex>_<n>.pxc` is the decoded pixel cache for that image — the source
extension is replaced with `.pxc`:

```c++
u16 width;
u16 height;
u8  rows[height][(width + 3) / 4];  // 2 bits per pixel, 4 per byte, MSB first
```

Pixel values run 0 (black) to 3 (white); uncovered rows and gaps stay 0. The
cache is written in bounded row bands (the band buffer is capped at 24 KB) as the
decoder emits raster MCU rows, so a full-page image never needs a
whole decoded frame in RAM. A failed or interrupted write removes the file.

A cache is rejected — and the image re-decoded — when its stored dimensions
differ from the expected ones by more than one pixel in either axis, or when the
file is shorter than `4 + ((width + 3) / 4) * height`.

## Covers and thumbnails

Every cover and thumbnail the firmware generates is a Windows BMP file with a
40-byte `BITMAPINFOHEADER`, negative `biHeight` (top-down rows), 72 DPI
(2835 px/m) and `BI_RGB`. Rows are padded to a 4-byte boundary. (A TXT cover
copied verbatim from the SD card keeps whatever header its source had.)

| Depth | Header size | Palette |
| --- | --- | --- |
| 1-bit | 62 bytes | black, white |
| 2-bit | 70 bytes | `0x00`, `0x55`, `0xAA`, `0xFF` |
| 8-bit | 1078 bytes | 256 grey entries |

The JPEG and PNG converters select 2-bit output for grayscale covers
(`USE_8BIT_OUTPUT` is `false`) and 1-bit output for thumbnails; the 8-bit path
exists but is not enabled.

- `cover.bmp` / `cover_crop.bmp` — the EPUB cover image converted from JPEG or
  PNG and scaled to the display's portrait dimensions (the runtime display
  height by its width), in fit and cropped variants.
- `thumb_<height>.bmp` — 1-bit, target size `0.6 * height` by `height`. When no
  usable cover exists an empty file is written so the home screen does not retry
  the conversion on every visit.
- XTC covers come from page 0: an XTCH 2-bit page is written as a 4-grey BMP so
  the grayscale pass can render it, a 1-bit page as a 1-bit BMP. An XTC thumbnail
  that would not be downscaled is copied from `cover.bmp` byte for byte, so an
  XTCH thumbnail keeps the 4-grey palette.
- TXT covers come from an image beside the `.txt` file: first one named after the
  book (`mybook.bmp`, `.jpg`, `.jpeg`, `.png`, upper-case variants included), then
  `cover`/`Cover`/`COVER` with the same extensions. BMP is copied verbatim and
  JPEG is converted; a PNG is found by the search but then rejected, because
  there is no PNG path here.

## Progress files

Every format writes progress the same way: the bytes go to `progress.bin.tmp`,
the old `progress.bin` is removed, and the temp is renamed into place. This is
crash-safe rather than atomic — a crash between the remove and the rename leaves
neither file, which simply reads as "no saved progress" — but `progress.bin` is
never left torn.

| Format | Size | Payload |
| --- | --- | --- |
| EPUB | 6 or 10 | `u16 spineIndex`, `u16 page`, `u16 chapterPageCount`, and (10-byte form) `u32 visibleTextOffset` |
| TXT | 4 | `u16 page`, two zero bytes |
| XTC | 4 | `u32 page` |
| FB2 | 8 | `u16 chapterIndex`, `u16 page`, `u16 pageCount`, `u16 marker` (`0xFB02`) |

The EPUB reader also accepts a 4-byte file (spine and page only) from older
firmware. The FB2 reader accepts 4- and 6-byte files from older firmware too, but
reads them differently: without the `0xFB02` marker the payload predates
one-chapter-per-`<section>` numbering, so its first field is a **top-level
ordinal** rather than a chapter index. It is resolved through
`Fb2::firstChapterOfTopLevel()` — the (n+1)-th level-0 chapter — and the page
restarts at 0, because the stored page indexed a chapter that no longer exists at
that size. The next save writes the 8-byte form, so each book migrates once. An
8-byte payload carrying any other marker is rejected. A saved EPUB page of
`0xFFFF` is a stale last-page sentinel and is read back as page 0. `saveProgress`
refuses values outside 0..65535. The TXT and XTC readers clamp the decoded page
into range on load.

## TXT `index.bin`

### Version 3

`txt_<hash>/index.bin` stores the byte offset at which each page starts, so
reopening a large text file does not repaginate it.

```c++
u32 magic;              // 'TXTI' = 0x54585449
u8  version;            // 3
u32 fileSize;           // size of the source .txt
s32 viewportWidth;
s32 linesPerPage;
s32 fontId;
s32 screenMargin;
u8  paragraphAlignment;
u32 numPages;
u32 pageStart[numPages];
```

Every header field after the version is part of the cache key: any mismatch
against the current file size or render settings discards the index and
repaginates behind the "Indexing" popup. Pagination itself streams the file in
8 KB chunks, splits on LF (dropping a trailing CR) and wraps at the last space or
a UTF-8 boundary.

## XTC caches

XTC books are read directly from the source file, so `xtc_<hash>/` holds no
layout cache — only `cover.bmp`, `thumb_<height>.bmp` and `progress.bin`,
described above.

## FB2 caches *(fork-only)*

### `book.bin` version 5

```c++
u8     version;         // 5
String title;           // u32 length + bytes, each header string <= 4096 bytes
String author;
String language;
String coverBinaryId;
u16    chapterCount;    // 1..256 (FB2_MAX_CHAPTERS)
u32    titlesSize;      // bytes in the title area
Record record[chapterCount];   // 20 bytes each, in chapter (start-tag) order
u8     titles[titlesSize];     // raw title bytes, no prefixes, no terminators

struct Record {         // 20 bytes, written field by field
  u32 titleOffset;      // into titles[]
  u16 titleLength;      // <= 4096; 0 = no title (the UI shows its "Unnamed" placeholder)
  u32 fileOffset;       // offset of the section's "<section" start tag in the source
  u32 ownLength;        // own bytes: full span minus child chapters' spans
  u32 cumulativeLength; // sum of ownLength over records 0..i
  u8  level;            // nesting depth; 0 = direct child of <body>
  u8  flags;            // bit0: title was derived from the first paragraph; other bits reserved 0
};
```

The book keeps no chapter list in RAM. Record *i* sits at a computed offset, so one
chapter is read with a seek and a 20-byte read, plus a seek and a read for its title. A
chapter's progress weight is `[cumulativeLength - ownLength, cumulativeLength)`, and the book
size is the last record's `cumulativeLength`, so progress needs one record, never a scan.

A section with no `<title>` of its own is labelled from its own first `<p>`, cut to
`FB2_MAX_LABEL_CHARS` (64) codepoints on a character boundary, and `flags` bit 0 records that the
title was derived rather than supplied by the book. A title the book does supply is stored as it
is written, cut to the 4096-byte string cap on a character boundary. A section with neither a
title nor text of its own stores an empty title.

Every `<section>` of a reading body is a chapter, numbered in start-tag order at
any depth, so the chapter list is the TOC: there is no separate TOC list, and
both index mappings are the identity. A body's own `<title>`/`<epigraph>`, which
precedes its first `<section>`, reads with that first chapter but is in no
chapter's `ownLength`. Chapter lengths exclude the spans of child
chapters, so they partition the reading bodies and reading progress stays
monotonic.

The file is built without holding chapters in RAM: the parser appends a zeroed record slot to
`chapters.tmp` at each start tag and patches it at the end tag (end tags arrive children
first), appending the title to `titles.tmp`. One sequential pass then writes `book.bin`,
filling in `cumulativeLength`, and both temp files are removed. A failed build removes
`book.bin` and both temp files.

Every read is validated before use, and any failure falls back to reparsing the FB2 file:

- `version == 5`, and each header string is at most 4096 bytes;
- `1 <= chapterCount <= FB2_MAX_CHAPTERS`, and the file is exactly
  `header + chapterCount * 20 + titlesSize` bytes, checked before any record is read;
- per record, in one sequential pass: `titleLength <= 4096` and
  `titleOffset + titleLength <= titlesSize`; no `flags` bit outside bit 0, and no derived
  marker on an empty title; a `level` at most one step past its predecessor, and 0 on the
  first chapter; `cumulativeLength` exactly the previous total plus `ownLength`.

A lookup that fails later (a card fault, a file changed after load) returns an empty chapter,
which the UI shows as its "Unnamed" placeholder.

Version 5 replaced the length-prefixed chapter list with fixed records and a title area, and
added `cumulativeLength`; a version-4 cache is rejected and the book's metadata reparsed, which
does not touch `sections/<index>.bin`. Version 4 added the `flags` byte. Version 2 stopped
counting auxiliary `<body name="...">` sections as chapters, which shifts section numbering for
books with footnote bodies. Version 3 makes every nested `<section>` a chapter of its own and
adds the `level` byte, which renumbers chapters for any book that nests sections; the chapter
count is capped, and past the cap a `<section>` reads as part of the chapter containing it
rather than becoming one.

### `sections/<n>.bin` version 5

FB2 sections are laid out with the EPUB page pipeline but track only the spec
fields that affect FB2 layout — there is no CSS and no image mode — so the
version is independent of the EPUB one. The 23-byte header is:

```c++
u8    version;          // 5
s32   fontId;
float lineCompression;
bool  extraParagraphSpacing;
u8    paragraphAlignment;
u16   viewportWidth;
u16   viewportHeight;
bool  hyphenationEnabled;
bool  focusReadingEnabled;
u16   pageCount;
u32   lutOffset;
```

Page records use the same encoding as the EPUB `Page` above, followed by
`u32 pageOffset[pageCount]` at `lutOffset`. A file shorter than the header, a
version mismatch, a render-spec mismatch, or a `lutOffset` that does not leave
room for the page data and the whole LUT clears the cache and rebuilds it.

Version 5 was bumped because the file *name* changed meaning: the chapter index
now counts every `<section>` at any depth, so a version 4 `sections/1.bin` holds
the old second top-level chapter and would silently render the wrong text. The
layout itself is unchanged. Version 4 was bumped because container block styles
(title centring, epigraph/cite indents) now reach wrapped `<p>` children; version
3 because section counting became top-level-only.

## JSON stores

Files directly under `/.crosspoint/` are UTF-8 JSON, serialized by ArduinoJson
and written whole through `PersistableStoreBase::writeDocToFile`, which creates
`/.crosspoint` first. A missing file is not an error (expected on first boot); a
parse error is logged and the store keeps its defaults. When a load upgrades a
legacy shape in memory, the store requests a resave and the file is rewritten
after the load completes.

Passwords are never stored in plaintext by the current firmware: they are
XOR-obfuscated against the device's MAC address and base64-encoded under a
`password_obf` key. A legacy plaintext `password` key is still read as a fallback
and triggers a resave in the obfuscated form. Because the key is the device's own
MAC, these files cannot be decoded on another device or on a PC.

| File | Store | Shape |
| --- | --- | --- |
| `settings.json` | `CrossPointSettings` | Flat object of user settings |
| `state.json` | `CrossPointState` | Runtime state carried across reboots |
| `recent.json` | `RecentBooksStore` | `{"books":[...]}`, most recent first |
| `opds.json` | `OpdsServerStore` | `{"servers":[...]}` |
| `wifi.json` | `WifiCredentialStore` | `{"lastConnectedSsid":str,"credentials":[...]}` |
| `koreader.json` | `KOReaderCredentialStore` | Sync account and options |
| `bookmarks/<book>.json` | `BookmarkFile` | `{"bookmarks":[...]}` |

### `settings.json`

Every entry in `getSettingsList()` that has a JSON key and either a `uint8_t`
member or a fixed `char[]` member is written under that key; a string entry
marked obfuscated is written as `<key>_obf` instead. Six keys are written outside
that loop: `frontButtonBack`, `frontButtonConfirm`, `frontButtonLeft`,
`frontButtonRight`, `fontFamily` and `fontSize`. `sdFontFamilyName` and
`dictionaryName` are written only when non-empty, `keyboardLayouts` (a `u16`
mask) only when non-zero, and `language` is stored as an ISO code string
(`"EN"`, `"DE"`, ...) rather than an enum index so it survives enum reordering.

Loading clamps every enum to its option count, resets the four front-button
assignments to defaults if any two collide, and migrates the legacy
`sleepTimeout` enum to `sleepTimeoutMinutes` when the newer key is absent.

### `state.json`

```json
{
  "openEpubPath": "/books/example.epub",
  "recentSleepImages": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
  "recentSleepPos": 0,
  "recentSleepFill": 0,
  "recentOverlaySleepImages": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
  "recentOverlaySleepPos": 0,
  "recentOverlaySleepFill": 0,
  "readerActivityLoadCount": 0,
  "lastSleepFromReader": false,
  "showBootScreen": true
}
```

Both sleep-image arrays are 16-slot ring buffers of image indices with their
write position and fill level, used to avoid repeating a sleep screen. A legacy
`lastSleepImage` scalar is migrated into the ring when the ring is empty.

### `recent.json`

Each of the at most ten entries carries `path`, `title`, `author` and
`coverBmpPath`. A missing or non-array `books` key loads as an empty list.

### `opds.json`

Each of the at most eight servers carries `name`, `url`, `username` and
`password_obf`.

### `wifi.json`

`lastConnectedSsid` plus at most eight `credentials`, each `ssid`,
`password_obf`, `password_len` and `password_crc32`. The stored length and CRC32
are checked against the deobfuscated password so a decodable-but-corrupted value
is discarded rather than used as a different password; passwords are capped at 64
characters.

### `koreader.json`

```json
{
  "cfgVersion": 2,
  "username": "",
  "password_obf": "",
  "serverUrl": "",
  "matchMethod": 0,
  "sendMetadata": false,
  "syncBehavior": 1
}
```

`matchMethod` is 0 for filename matching and 1 for partial-MD5 (binary)
matching; `syncBehavior` is 0 to ask every time and 1 for smart resolution. A
config written before version 2 that has credentials but no explicit `serverUrl`
is pinned to the previous default sync server so an upgrade does not silently
switch servers, and the file is resaved with the current `cfgVersion`.

### `bookmarks/<book>.json`

The filename is the book path with the leading slash removed, every `/` and `\`
replaced by `_`, and the extension dropped in favour of `.json`, so all bookmark
files live flat in `/.crosspoint/bookmarks/`.

```json
{"bookmarks": [{"xpath": "", "percentage": 0.0, "summary": "", "si": 0, "pc": 0, "pp": 0, "vo": 0}]}
```

`si` is the spine index, `pc` the chapter's page count and `pp` the page number
within the chapter, all recorded when the bookmark was made. `percentage` is a
float in 0..1. `vo` is the page's visible-codepoint offset within the spine item;
it is written only when known, and because it is immune to re-pagination it is
what lets a bookmark land on the right page after a font, margin or orientation
change. `summary` is whitespace-collapsed, newline-stripped and truncated to 72
bytes before saving.
