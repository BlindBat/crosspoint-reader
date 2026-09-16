# Dictionary

Look up words while reading an EPUB using an offline StarDict dictionary stored on the SD card.

## Supported Format

The reader supports **StarDict** dictionaries. When searching for dictionaries online, look for "StarDict format" or files with `.dict`, `.idx`, and `.ifo` extensions.

A dictionary folder must contain:

- `.idx` — word index (required, **must be uncompressed** — a `.idx.gz` will not work; decompress it on your computer with `gzip -d` first)
- `.dict` or `.dict.dz` — definition data (`.dict.dz` is supported as-is; entries are decompressed on the fly during lookup)
- `.syn` — synonym index (optional; maps alternate spellings and irregular forms to their headword)
- `.ifo` — metadata (optional)

Not supported: dictionaries with 64-bit index offsets (`idxoffsetbits=64` in the `.ifo`) — rare. Folder discovery never reads the `.ifo`, so such a dictionary still appears in the Dictionary list; it is rejected when a lookup opens it, and the lookup reports *"Dictionary error"*.

## Setting Up a Dictionary

1. Copy your dictionary folder(s) to `/dictionaries/` on the SD card — one dictionary per folder, e.g. `/dictionaries/webster/webster.idx` + `webster.dict.dz`. A hidden `/.dictionaries/` folder (dot-prefixed) works the same way, for keeping it out of the file browser: both roots are scanned, `/dictionaries/` first, and the combined list is sorted case-insensitively by folder name. Folders whose own name begins with `.` are skipped, and a dictionary folder name must be 31 characters or shorter — the setting keeps only the first 31 characters, so a longer name is saved truncated, no longer resolves to a folder on the card, and every lookup then reports *"Dictionary error"*.
2. Open **Settings → Reader → Dictionary** on the device.
3. Select a dictionary from the list, or **None** to disable lookups.

The Dictionary setting only appears when at least one usable dictionary folder exists. Folders containing more than one dictionary (multiple `.idx` stems) are skipped as ambiguous, as are folders with an `.idx` but no `.dict` or `.dict.dz` beside it.

## Looking Up a Word

Two ways to start a lookup while reading:

- Open the reader menu (**Confirm**) and choose **Look Up**.
- Or set **Settings → Controls → Long-press Menu** to "Dictionary", then hold **Confirm** (~0.4s) on the reading page.

One word on the page becomes highlighted:

1. Use **Left/Right** to move between words in reading order, and the side **Up/Down** buttons to jump between lines.
2. Press **Confirm** to look up the highlighted word.
3. Press **Back** to return to the reader.

On the very first lookup with a dictionary (and again whenever the `.idx` or `.syn` changes size), the reader shows *"Indexing dictionary..."* while it builds small sidecar files next to them — a `.qidx` for the word index, and a `.sidx` when a `.syn` synonym file is present. Each sidecar is rebuilt independently, only when its own source changed size. This takes a few seconds for large dictionaries and makes all subsequent lookups fast. The sidecars can be deleted safely at any time — they will simply be rebuilt.

### How Lookup Works

1. **Direct match** — the word is looked up as-is in the dictionary index. Punctuation around it is stripped first, including curly quotes and dashes (U+2000–U+206F), so `garage.”` still matches `garage`. The comparison is case-insensitive **for ASCII only**: `The` matches `the`, but a non-ASCII letter is compared byte for byte, so a capitalised `É`, `Ä` or `Ж` only matches a headword carrying the same capitalisation. Headwords longer than 255 bytes are truncated as the index is read and can never be matched.
2. **Synonyms** — on a miss, if the dictionary ships a `.syn` file, alternate spellings and irregular forms recorded there are resolved to their headword (e.g. `oxen` → `ox`, `colour` → `color`). This step is skipped whenever the `.sidx` sidecar is unusable — missing, stale, corrupt, or never built (e.g. transient low memory during indexing) — because the `.syn` is never scanned without it. The synonym probe then never reaches a verdict, so a word that neither the direct match nor a stem variant finds is reported as *"Couldn't read definition"* rather than *"Not found"*. The dictionary otherwise stays usable, and the sidecar build is retried the next time a lookup opens it.
3. **Stemming** — still no match: a small fixed set of **English** word forms is retried automatically: possessives (`dog's`, `dog’s` → `dog`), plurals (`dogs` → `dog`, `boxes` → `box`, `stories` → `story`) and verb endings (`walked` → `walk`, `loved` → `love`, `stopped` → `stop`, `walking` → `walk`, `making` → `make`, `running` → `run`). There is no stemming for any other language, so a non-English dictionary depends on its `.syn` file for inflected forms.
4. **Not found** — a popup appears for about 1.5 seconds and you return to word selection. Failures are named apart from a genuine miss: *"Dictionary error"* (the dictionary could not be opened), *"Couldn't read definition"* (an index or data file could not be read), *"Couldn't decompress definition"* (corrupt or truncated `.dict.dz`) and *"Not enough memory"*.

## The Definition Screen

When a word is found, the definition screen shows the matched headword at the top and the definition text below, with a page counter for long definitions. At most the first 64 KB of an entry is read; anything past that is cut off. Reading a definition also needs roughly 8 KB of contiguous heap above the entry's own size, or the lookup reports *"Not enough memory"* instead of opening this screen.

HTML dictionaries that declare `sametypesequence=h` use the EPUB text-layout engine for semantic formatting such as headings, bold, italics, lists, and line breaks. Images and CSS styling are ignored. Definitions larger than 16 KB, and definitions that cannot be laid out within the available memory, fall back to plain text.

- **Left/Right** or side **Up/Down** — previous / next page
- **Back** — return to word selection



## Where to find dictionaries

> credit to https://github.com/koreader/koreader/wiki/Dictionary-support for the list.

- The [reader.dict](https://www.reader-dict.com) (ex "BoboTiG/ebook-reader-dict") project provides StarDict version of daily dumps of [Wiktionary](https://www.wiktionary.org/) monolingual dictionaries for a variety of languages. It also provides [non-free multilingual](https://www.reader-dict.com) dictionaries.
- The [WikDict](https://www.wikdict.com) project provides free bilingual dictionaries based on [Wiktionary](https://www.wiktionary.org/) for a lot of language pairs. StarDict versions can be [downloaded from here](https://download.wikdict.com/dictionaries/stardict/).
- The [`Vuizur/Wiktionary-Dictionaries`](https://github.com/Vuizur/Wiktionary-Dictionaries) repository contains dictionaries based on [Wiktionary](https://www.wiktionary.org/) from many languages to English, including English-English.
- The [DictInfo](https://www.dictinfo.com/) website provides outdated monolingual dictionaries based on [Wiktionary](https://www.wiktionary.org/).
- The [Firedict site](https://tuxor1337.frama.io/firedict/dictionaries.html) contains a list of freely available dictionaries.
- [wiktionary_stardict](https://xxyzz.github.io/wiktionary_stardict/): update monthly.
- [Fictionaries](https://fictionary.gumroad.com/) provides dictionaries for various speculative fiction books and series.
- [World Factbooks Archive](https://github.com/MilkMp/CIA-World-Factbooks-Archive-1990-2025) provides 36 years of CIA's World Factbook dictionaries in StarDict format.
- [StarDict-Hebrew](https://github.com/Uri-Tauber/StarDict-Hebrew) Hebrew-English StarDict versions of Babylon dictionaries.
