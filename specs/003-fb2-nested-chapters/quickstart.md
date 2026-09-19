# Quickstart: Validating FB2 Nested Chapter Navigation

How to prove the feature works. Host checks come first (fast, and they are the merge
gates); the simulator confirms what the reader actually sees; the reference book confirms
the spec's numbers.

## Prerequisites

- PlatformIO installed (`pio`), Xcode CLT on macOS — `bin/run-tests` finds the rest from
  the PlatformIO packages. See the host-toolchain note in `bin/run-tests --help` if
  `CC`/`CXX` need pointing at the system clang.
- The reference book on the test card: `fs_/Брэдбери Рэй/Марсианские хроники. Полное издание.fb2`
  (1,915,806 bytes).
- Simulator env configured in the gitignored `platformio.local.ini` (see the
  `run-simulator` skill).

## 1. Prove the pins fail first (Principle V)

Before touching production code, rewrite the four tests that pin the old behaviour and run
them against unmodified sources — each MUST fail:

```bash
bin/run-tests --filter fb2_metadata_parser   # NestedSections… numbering/level/title
bin/run-tests --filter fb2_section_parser    # extent + parity tests
```

Record the failures (they are the mutation evidence the constitution asks for), then
implement.

## 2. Host suites

```bash
bin/run-tests --filter fb2          # fast loop while implementing
bin/run-tests                       # full program, plain
bin/run-tests --asan                # full program, ASan + UBSan (leak checks on)
```

Expected, keyed to the contract in [contracts/chapter-model.md](contracts/chapter-model.md):

| Check | Fixture | Expectation |
|-------|---------|-------------|
| Numbering (C2) | `nested-sections.fb2` | 3 chapters, levels `0,1,0`, titles `Part One The Beginning` / `Inner Chapter` / `Part Two` |
| Extent (C3) | `nested-sections.fb2` | chapter 0 has both outer paragraphs and no `innerword`; chapter 1 has `innerword` only |
| Parity + completeness (C3/C7) | every `test/fb2/*.fb2` | chapters `0..count-1` together contain every reading-body word exactly once; target `count` renders nothing |
| Title scoping (C4) | new deep-nesting fixture | a `<poem><title>` and a child section's title never become the parent's title |
| Empty chapter (C8) | new wrapper-only fixture | chapter renders exactly 1 page; `pageCount != 0` |
| Cap (C6) | generated >1024-section fixture | chapter count == 1024, no text lost, no OOM under ASan |
| Cache round-trip | `fb2_book` | v3 write → read yields identical titles, lengths and levels; a v2 file is rejected and reparsed |
| Malformed input | `corpus` | unbalanced sections, 64-deep nesting, count claiming 65535 → bounded failure, sanitizer-clean |
| Progress migration | `xtc_fb2_readers` | 8-byte marker payload round-trips; 6-byte payload decodes as a legacy ordinal with page 0; flat book → identity |

## 3. Firmware build and static analysis

```bash
pio run -e default                                                    # ESP32-C3
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
./bin/clang-format-fix -g
```

## 4. Simulator walk-through (the user-visible part)

```bash
# via the run-simulator skill, or directly:
pio run -e simulator
```

With the reference book on the simulator's SD card:

1. Open the book → chapter list (reader menu → chapters). **Expect 66 entries**, the four
   part-level entries un-indented and the story entries indented one step under them
   (SC-001, SC-002).
2. Select "Ракетное лето" → it opens at its first page; the page counter shows that
   story's own page count, not the part's (US1/US3).
3. Page back from its first page → previous chapter's last page. Page forward past its
   last page → next story's first page (FR-008).
4. Watch the status-bar percentage across three or four story boundaries — it should step
   by a few percent, not by ~25% (SC-005).
5. Reopen the chapter list while inside a story → that story's row is pre-selected
   (US2 scenario 3).
6. Confirm the 14 footnote sections of the `name="notes"` body are absent from the list
   (FR-010).

## 5. Legacy-position check (FR-012)

1. On the **current** firmware, open the reference book, page into "Иные марсианские
   хроники" (old chapter index 2), exit so progress is saved.
2. Flash/run the new build without deleting the SD cache.
3. Reopen: expect to land at the **start of "Иные марсианские хроники"** (its part-level
   chapter), not in an unrelated story and not at the book's start.
4. Page once, exit, reopen → position now restores exactly (the 8-byte form was written).

## 6. Re-measure the spec's numbers (Principle IV)

```bash
python3 - "fs_/Брэдбери Рэй/Марсианские хроники. Полное издание.fb2" <<'PY'
import sys, re
t = open(sys.argv[1],'rb').read().decode('utf-8','replace')
body = 0; stack = []; sizes = []
for m in re.finditer(r'<(/?)(body|section)\b', t):
    close, tag = m.group(1), m.group(2)
    if not close:
        if tag == 'body': body += 1; stack = []
        elif body == 1: stack.append((len(stack), m.start()))
    elif tag == 'section' and body == 1:
        d, st = stack.pop(); sizes.append((d, m.start() - st))
print('chapters in body 1:', len(sizes), 'levels:', sorted({d for d, _ in sizes}),
      'largest section bytes:', max(s for _, s in sizes))
PY
```

Expected: `chapters in body 1: 66 levels: [0, 1] largest section bytes: 67209`.

## Definition of done

All of section 2 green plain **and** under `--asan`; section 3 clean; section 4 walked in
the simulator; section 5 verified once on a pre-existing cache; `docs/file-formats.md`
updated for `book.bin` v3, `sections/<n>.bin` v5 and the 8-byte `progress.bin`.
