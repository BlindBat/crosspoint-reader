# Contract: StarDict dictionaries

Folder convention: `/dictionaries/<folder>/` or `/.dictionaries/<folder>/` (searched in that order), one dictionary per non-dot folder, exactly one `<stem>.idx` (uncompressed) plus `<stem>.dict` (preferred) or `<stem>.dict.dz`; optional `<stem>.syn` and `<stem>.ifo`; `._*` files ignored; folders with two `.idx` stems or without a data file are not listed. Folder names must not start with `.` or contain `/` or `\`; `<basePath>.dict.dz` must fit 160 bytes.

Files:
- `.idx`: entries of `headword\0`, `u32 BE offset`, `u32 BE size`, sorted ASCII-case-insensitively; headwords over 255 bytes are truncated for comparison.
- `.syn`: entries of `word\0`, `u32 BE ordinal` (0-based index of the `.idx` entry).
- `.ifo`: only `idxoffsetbits` (`=64` → rejected) and `sametypesequence` (exactly `h` → HTML definitions) are read from the first 2 KB.
- `.dict.dz`: gzip with `FEXTRA` containing exactly one `RA` subfield (`u16 LE version` = 1, `u16 chunkLength` > 0, `u16 chunkCount` 1..8192, `subLen == 6 + 2×chunkCount`, then `chunkCount` `u16` compressed sizes); `FNAME`/`FCOMMENT`/`FHCRC` skipped; ISIZE trailer must be non-zero. Only the chunks covering the requested range are inflated (2 KB input pieces, 32 KB ring) into `/.crosspoint/dict.tmp`.
- Generated sidecars `.qidx`/`.sidx`: see cache-formats.md; rebuilt when missing, corrupt, of another version/interval, or built from a different source size; the `.syn` sidecar failing only disables synonyms.

Lookup: clean the token (strip leading/trailing bytes that are not ASCII alphanumeric or ≥ 0x80, strip U+2000–U+206F, ASCII-lowercase) → exact (bisect the sidecar samples to the last headword ≤ target, scan forward ≤256 entries, stop at the first headword > target; without a sidecar scan from byte 0) → synonyms (ordinal resolved through the `.qidx` samples; skipped, with misses reported as ReadError, when the `.sidx` is unusable) → stems (`'s`, `’s`, `ies→y`, `es`, `s`, `ed` ×3, `ing` ×3). Definitions: size 0, offsets/sizes outside the data file → ReadError; > 64 KB truncated; refused with LowMemory when the largest free block < size + 8 KB. Results: Found, NotFound, LowMemory, Decompress, ReadError.

Definition rendering: HTML (`sametypesequence=h`, ≤16 KB, ≥40 KB free / 20 KB block) → normalised XHTML (`<html><body>` wrapper, lowercase tags, self-closed voids, stray closers and `<!…>`/`<?…>` dropped, bare `<`/`&` escaped) laid out by the EPUB parser with the reader's typography (CSS off, images suppressed; ≤64 pages, ≤512 elements; retain gate 16 KB / 8 KB); otherwise NULs → newlines, tags stripped, entities decoded, greedy word wrap.
