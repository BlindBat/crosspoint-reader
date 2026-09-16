# SD Card Fonts

CrossPoint supports loading additional fonts from the SD card, including fonts
with extended Unicode coverage (CJK, Cyrillic, Greek, etc.).

## Installing Fonts

There are three ways to install fonts:

### Option 1: Download from device (recommended)

1. Go to **Settings > Reader > Manage Fonts**. The reader asks you to join a
   Wi-Fi network before it fetches the font list
2. If the font list declares script groups, a group screen opens first: **All
   fonts** plus one row per script, each showing how many families it holds.
   Pick one to filter the family list below it; **Back** returns to the group
   screen
3. Pick a family to download it. Above the families, a **Download All** row
   appears whenever the current group still has something uninstalled, and an
   **Update All** row whenever it has an update; both show the combined size
4. Every downloaded file is verified against the manifest's CRC32 and its
   `CPFONT` magic bytes. A failed or cancelled download deletes the whole
   family directory again, so nothing is left half-installed
5. Picking a family that is already installed and up to date opens a **Delete**
   confirmation instead. Deleting removes the family from both font roots and,
   if that family was the active reader font, clears the selection
6. Leaving the screen disconnects Wi-Fi and silently restarts the reader (this
   recovers the heap the Wi-Fi session fragmented). Installed families are
   listed in **Settings > Reader > Text Settings**, under the **Font** tab

### Option 2: Upload via web browser

1. Start **File Transfer** and connect through **Join a Network** or **Create Hotspot**
2. Open the web interface URL shown on the reader
3. Navigate to the **Fonts** tab
4. Upload `.cpfont` files using the upload form

The page derives the destination family folder from the file name — everything
before the last `-` or `_` — so the files you upload must be named
`<Family>_<size>.cpfont`, and every file in one upload has to resolve to the
same family. Characters outside `A-Za-z0-9_-` are replaced with `_`, and the
firmware rejects any family or file name that still contains something else.

### Option 3: Manual SD card copy

1. Download font files from the
   [crosspoint-fonts repository](https://github.com/crosspoint-reader/crosspoint-fonts)
2. Copy font family folders to one of two locations on your SD card:

   - `/.fonts/` — hidden directory (preferred; keeps the SD root tidy
     when mounted on a desktop)
   - `/fonts/` — visible directory (use this if your OS hides dot-files
     and you'd rather see the folder in your file manager)

   Both roots are always scanned at boot and the results are merged: a
   family installed in `/fonts/` shows up even when `/.fonts/` also
   exists, and vice versa. The two roots only collide if the same family
   name appears in both — in that case the copy in `/.fonts/` wins and
   the duplicate in `/fonts/` is ignored. Downloads and web uploads are
   written to whichever root already holds that family; for a brand-new
   family they go to `/.fonts/`, unless only `/fonts/` exists on the card.

       SD Card Root/
       ├── .fonts/                     ← Hidden root (preferred)
       │   └── Literata/
       │       ├── Literata_12.cpfont
       │       ├── Literata_14.cpfont
       │       ├── Literata_16.cpfont
       │       └── Literata_18.cpfont
       └── fonts/                      ← Visible root (equally valid)
           └── Merriweather/
               ├── Merriweather_12.cpfont
               └── ...

   The folder name is the family name, and the part of the file name before
   `_<size>` is ignored: `Literata_14.cpfont` and `Serif_14.cpfont` both
   register as Literata at 14 pt when they sit in `/.fonts/Literata/`. A file
   only counts if it ends in exactly `.cpfont` (`Literata_14.cpfont.tmp` and
   `Literata_14.cpfont~` do not) and its size reads as 1–255. Files and
   folders whose names start with `.` or `_` are skipped, which keeps macOS
   `._` resource forks out. If two files in one family claim the same point
   size, the second one is ignored. At most 128 families are kept, sorted by
   name. Keep the family folder name to 31 characters or fewer: the reader
   stores the selected family in a 32-byte field, so a longer name is
   truncated when saved, no longer matches the folder on disk, and the
   selection is dropped.

3. Insert the SD card and power on your CrossPoint reader

## CJK in the User Interface

The built-in UI fonts carry no CJK glyphs — they cover Latin (including
Vietnamese), Cyrillic, Hebrew and Arabic — so by default the interface (book
titles in the library, file names in the browser, list rows, headers) shows
replacement boxes for Chinese/Japanese/Korean text even when book *content*
renders correctly with a selected SD-card font.

To avoid shipping a large CJK glyph set in flash, CrossPoint instead reuses the
SD-card font you already selected: when a UI string contains a CJK character
the built-in font cannot draw, that whole string is rendered with your selected
SD-card font instead.

The fallback is **size-matched**. The built-in UI fonts render at 8 pt
(header status text and button hints), 10 pt (list subtitles, the home-screen
author line) and 12 pt (list rows, headers, book-cover titles), so CrossPoint
loads your SD family at those sizes too and maps each UI font to its same-size
SD font. CJK book names therefore appear at the same size as the
Latin text around them. For this to work the family must contain `.cpfont`
files at sizes **8, 10 and 12** (in addition to the reader sizes 12–18); any UI
size missing from the family simply keeps showing boxes for CJK at that size.
The UI sizes are only loaded at all when the selected family really does carry
CJK: CrossPoint first probes the loaded reader-size font for U+4E00, U+3042,
U+30A2 and U+AC00, and skips the fallback sizes entirely for a Latin-only
family rather than spend RAM on glyphs it could never redirect to.

Note that the **Size** tab of **Settings > Reader > Text Settings** lists every
size the family ships, so a family built at 8,10,12,14,16,18 offers all six as
reading sizes — the UI sizes are not hidden from the list. Reading at 8 pt is
your call; if you would rather not see the small sizes there, convert two
families (one with the UI sizes for fallback, one with only the reading sizes
you want).

Changing family keeps your reading size only if the new family ships it:
otherwise the size snaps to the nearest one it does ship (a tie picks the
smaller), and going back to a built-in family snaps it into 12/14/16/18.

When converting your own font, include the UI sizes:

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
      MyCJKFont-Regular.otf \
      --intervals cjk \
      --sizes 8,10,12,14,16,18 \
      --style regular \
      --name MyCJKFont \
      --output-dir ./MyCJKFont/

What this means in practice:

- Select a CJK-capable SD font on the **Font** tab of **Settings > Reader >
  Text Settings** (see [Installing Fonts](#installing-fonts) and the `cjk` /
  `hangul` presets under
  [Converting Custom Fonts](#converting-custom-fonts)). That single
  selection drives both book content *and* size-matched CJK fallback in the UI.
- Pure-Latin UI strings keep the crisp built-in font; only strings that
  actually contain CJK are routed to the SD font.
- The fallback is per *string*, not per glyph: a mixed title such as
  `三体 Vol.1` renders entirely in the SD font (including the Latin part). If
  that SD font is a `Mono` family, the Latin portion will appear half/full
  width.
- If no SD font is selected (a built-in reading font is active), there is no
  CJK fallback and the UI again shows boxes for CJK — pick a CJK SD font to
  restore it.

## Available Pre-Built Fonts

The current list of pre-built fonts is maintained in the
[crosspoint-fonts repository](https://github.com/crosspoint-reader/crosspoint-fonts).

## Converting Custom Fonts

To convert your own TrueType/OpenType fonts:

### Prerequisites

    pip install freetype-py fonttools

### Single font (one style)

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
      MyFont-Regular.ttf \
      --intervals latin-ext \
      --sizes 12,14,16,18 \
      --style regular \
      --name MyFont \
      --output-dir ./MyFont/

### Multi-style font

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
      --regular MyFont-Regular.ttf \
      --bold MyFont-Bold.ttf \
      --italic MyFont-Italic.ttf \
      --bolditalic MyFont-BoldItalic.ttf \
      --intervals latin-ext \
      --sizes 12,14,16,18 \
      --name MyFont \
      --output-dir ./MyFont/

### Available Unicode interval presets

| Preset | Coverage |
|--------|----------|
| `ascii` | U+0020–U+007E (Basic Latin) |
| `latin1` | U+0080–U+00FF (Latin-1 Supplement) |
| `latin-ext` | European languages (Latin + Extended-A/B + punctuation + ligatures) |
| `greek` | Greek + Extended Greek |
| `cyrillic` | Cyrillic + Supplement |
| `hebrew` | Hebrew + Alphabetic Presentation Forms |
| `arabic` | Arabic + Supplement + Extended-A + Presentation Forms A/B (RTL, contextual shaping) |
| `georgian` | Georgian + Georgian Supplement |
| `armenian` | Armenian |
| `ethiopic` | Ethiopic + Extended |
| `vietnamese` | Vietnamese subset (ơ/ư plus the precomposed tone-mark letters) |
| `ipa-chars` | IPA Extensions + Spacing Modifier Letters (phonetic transcription) |
| `punctuation` | General punctuation (U+2000–U+206F) |
| `cjk` | CJK symbols/punctuation + Hiragana + Katakana + Unified Ideographs + Compatibility Ideographs + Half/Fullwidth Forms |
| `hangul` | Korean Hangul syllables + Jamo + Compatibility Jamo |
| `cherokee` | Cherokee + Cherokee Supplement |
| `tifinagh` | Tifinagh |
| `symbols` | Super/subscripts, currency, number forms, arrows, math, box-drawing, geometric shapes, misc symbols, dingbats |
| `reading` | Literary fiction coverage: Latin, Greek, Cyrillic, math/symbol blocks, supplemental punctuation, and CJK quote marks |
| `builtin` | Matches the firmware's built-in font conversion intervals |

Combine presets with commas: `--intervals latin-ext,greek,cyrillic`

You can also specify arbitrary Unicode ranges directly:
`--intervals latin-ext,(0x2100-0x214F)`

To list all presets with codepoint counts:

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py --list-presets

### Additional options

`--force-autohint` — force FreeType's auto-hinter instead of the font's native hinting (useful when a font's built-in hints produce poor results at small sizes).

Install custom fonts via the web interface or manual SD card copy.
