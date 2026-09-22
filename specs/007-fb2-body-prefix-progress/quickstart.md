# Quickstart: Validating FB2 Body Front-Matter Progress Weight

How to prove this feature works, and how to prove it broke nothing. Run from the repo root
on `feature/fb2-body-prefix-progress`.

## Prerequisites

- Host toolchain for `bin/run-tests` (on macOS, `CC`/`CXX` must point at the Xcode clang).
- PlatformIO for the firmware build gate.
- Optional, for the device check: an FB2 book whose text starts before its first
  `<section>`. The corpus's worst case is `~/Calibre Library/Po/Moie tielo - Bosfor (36919)/`.

## 1. The pin fails before the fix (Constitution V)

The mutation proof is the fix itself: stash the parser change and the new pins must fail.

```bash
bin/run-tests --filter fb2_metadata_parser   # expect the weight pins RED before Phase C
```

Expected on `test/fb2/body-prefix.fb2` (body tag at 188, first `<section>` at 314, so 126 B
of front matter):

| | before | after |
|---|---|---|
| chapter 0 `length` | 110 B | **236 B** |
| chapter 1 `length` | 113 B | 113 B |
| `bookSize` | 223 B | **349 B** |
| chapter 0's share of the book | 49.3% | **67.6%** |

## 2. The partition still holds

```bash
bin/run-tests --filter 'fb2_book|fb2_metadata_parser'
```

For every fixture in `test/fb2/`, the summed chapter lengths must equal the oracle in
`fb2test::topLevelSectionBytes` — which, after Phase A, counts each reading body from its
`<body>` tag. The oracle is re-derived from the source text, not from the parser, so the
two agreeing is real evidence (contract rule C7′).

## 3. Nothing renders differently (FR-003)

```bash
bin/run-tests --filter fb2_section_cache
```

The page hashes for `body-prefix.fb2` chapters 0 and 1 (`0x261012990916872b`,
`0xb1edbf448284d2a1`) and every other fixture's must be **unchanged**. A changed hash means
the span widening leaked into rendering and the change is wrong.

## 4. Full gates

```bash
./bin/clang-format-fix -g
bin/run-tests
bin/run-tests --asan
pio run -e default
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
```

On macOS gates 2, 3 and 5 are weaker than CI's (libc++ vs libstdc++; `pio check` analyses
nothing and still reports PASSED). Read the CI run before calling this clean; use
`./bin/run-tests-linux` to reproduce CI's sanitizer coverage locally.

## 5. Cache behaviour on a real card (FR-009, FR-010)

With a v5 cache already on the SD card:

1. Note the chapter and page an FB2 book is open at, and its percentage.
2. Flash the new firmware and reopen the book.
3. Expect: same chapter, same page (`progress.bin` is untouched); `book.bin` silently
   rebuilt; `sections/` **not** dropped — a >256-chapter book must not sit through a full
   relayout, which is what pinning the cap clause to pre-v5 caches buys (research R3).
4. Reopen once more: no rebuild, cache reused.

## 6. Device check (human step)

On hardware, open a book with substantial front matter and read forward through it.

- **Before**: the percentage holds at its opening value until the first `<section>`, then
  jumps.
- **After**: it climbs page by page at the same rate as the rest of the book.

For the corpus's worst case that is the difference between 39% of the book at a frozen
number and a percentage that moves. Confirm with `[MEM]` serial output that free heap is
unchanged — this feature allocates nothing.
