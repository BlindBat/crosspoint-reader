# Contract: OPDS catalog consumption

- Servers: ≤8 in `opds.json` (name ≤63, url ≤127, username ≤63, password ≤63 obfuscated). The Home entry appears when ≥1 server exists; one server opens directly, more show a picker.
- Fetch: `GET <url or resolved href>` via the shared HTTP client with Basic auth when both credentials are set; 60 s timeout; ≤5 redirects; final status 200.
- Parsing (expat SAX, 1 KB chunks; element names matched by local name):
  - Top-level `<link rel="search" href="…{searchTerms}…">` → search template (only with the placeholder).
  - Top-level `<link rel="next">` / `rel="previous"` → pagination URLs (links inside entries ignored).
  - Inside `<entry>`: `<link rel~="opds-spec.org/acquisition" type="application/epub+zip" href>` → BOOK (an href containing `.epub` or `/epub/` replaces an earlier acquisition href); `<link type~="application/atom+xml" href>` → NAVIGATION unless already BOOK; `<title>`, `<author><name>`, `<id>` captured with caps 160/120/128 bytes; href ≤768.
  - Entry emitted at `</entry>` only with a non-empty title and href; ≤62 entries, further ones set the truncated flag.
  - Any XML error latches an error and the browser shows "Failed to parse feed".
- URL resolution: absolute URLs as-is; a leading `/` replaces the path on the feed's host; relative paths join the feed URL with its query stripped; unsafe bytes percent-encoded (existing `%XX` kept); a URL without `://` gets `http://`.
- Search: the query is percent-encoded (alnum `-_.~` kept) and substituted for the first `{searchTerms}`; fetched as a new page with history pushed.
- Download: `<opdsDownloadFolder or />/<stem>.epub` where stem = "Author - Title" | "Title - Author" | "Title" (title alone when author empty), sanitised (`/\:*?"<>|` → `_`, controls dropped, leading spaces/dots skipped, trailing trimmed), ≤100 bytes on UTF-8 boundaries, fallback `book`. Existing file replaced; zero bytes = failure; partial removed on abort/failure; book cache cleared on success. Refused below 40,000 bytes free heap or a 20,000-byte largest block after releasing the catalog rows and SD font caches. Progress repainted every 5% or 5 s; Cancel, Back or the home gesture abort.
- Errors shown: `No server URL configured`, `Failed to fetch feed`, `Failed to parse feed`, `No entries found`, `Download failed`; Confirm/tap retries (re-running Wi-Fi selection if the link dropped).
