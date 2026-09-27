# Contract: `Fb2::loadMetadata`

Mirrors `Epub::loadMetadata(std::string& title, std::string& author)` (upstream `1.6.5rc`,
`lib/Epub/Epub.cpp`) so `LibraryBuilder::stageRecord` can treat the two formats identically.

```cpp
// lib/Fb2/Fb2.h
// Title and author for the Library index. Reads the existing book.bin when it
// is valid; otherwise parses the file head only (stops at </title-info>).
// Never builds the chapter index, never writes to the cache directory.
bool loadMetadata(std::string& title, std::string& author);
```

## Behaviour

| Precondition | Result | Side effects |
|---|---|---|
| `book.bin` (v5) present and passes `loadMetadataCache()` | `true`; `title`/`author` from the cache header | none (read-only) |
| no / invalid `book.bin`, well-formed FB2 | `true`; `title`/`author` from `<title-info>`; both NFC-composed | reads the file up to `</title-info>`; nothing written |
| `<title-info>` absent or empty | `true` with empty strings (caller falls back to the filename stem) | as above |
| file unreadable, not XML, malformed before `</title-info>` | `false`; both strings cleared | nothing written |
| `<body>` reached before `</title-info>` (no description) | `true` with empty strings (caller falls back to the filename stem; status stays `EXTRACTED`) | parse stopped at `<body>` — the body is never read; no "did title-info close" state is kept |

- The parser is the existing `Fb2MetadataParser` with a metadata-only flag; the flag makes
  `endElement("title-info")` (and `startElement("body")`) call `XML_StopParser(parser, XML_FALSE)`,
  and `parse()` treats `XML_ERROR_ABORTED` as success when the flag is set.
- No chapter sink is invoked in metadata-only mode (the sink is null).
- Memory: the parser's 1 KB expat buffer and the strings; no `std::vector` growth (no chapters).
- Encoding: windows-1251/1252 files are handled by the same `fb2RegisterExtraEncodings` hook the
  full parse uses.

## Caller contract (`LibraryBuilder`)

```text
isBookName(name)          : … || FsHelpers::checkFileExtension(name, ".fb2")
extractionExpected        : readMetadata && (hasEpubExtension(name) || hasFb2Extension(name))
extract                   : hasFb2Extension ? Fb2(fullPath, CACHE_DIR).loadMetadata(t, a)
                                            : Epub(fullPath, CACHE_DIR).loadMetadata(t, a)
```

`metadataStatus`, fold, author key and display-author cleaning follow the EPUB branch unchanged.

## Tests (Principle V, each with the mutation it catches)

| Suite | Case | Mutation caught |
|---|---|---|
| `library_builder` | an `.fb2` on the card is indexed and counted | remove `.fb2` from `isBookName` |
| `library_builder` | with metadata on, an `.fb2` row takes title/author from `Fb2::loadMetadata` (stub) and `parses` increments once | drop the FB2 branch of `extractionExpected` |
| `library_builder` | `loadMetadata` failure → status `FAILED`, title = stem | swap the failure branch |
| `fb2_metadata_parser` | metadata-only parse on a book with sections: title/author correct, sink never called, bytes consumed < offset of `<body>` | remove the `XML_StopParser` call |
| `fb2_book` | `loadMetadata` on a cached book reads the header and opens no other file | route through `load(true)` instead |
