# Feature Specification: Upstream 1.6.5rc Sync

**Feature Branch**: `sync/upstream-1.6.5rc`

**Created**: 2026-09-26

**Status**: Draft

**Input**: User description: "with /ponytail full Upgrade to the origin's 1.6.5rc, keeping all the features in my fork and applying all the enhancements to them, as well"

## Summary

Two names, used throughout: **upstream** is `crosspoint-reader/crosspoint-reader` (the
"origin" of the request); the **fork** is `BlindBat/crosspoint-reader`.

Where the two stand today (measured 2026-09-26 with `git`, fork `master` at `b68be959`):

| | Base | Commits since base | Files touched |
|---|---|---|---|
| Fork `master` | upstream 1.6.0 (`54337e6d`, 2026-09-05) | 242 | 967 |
| Upstream `1.6.5rc` (`a1ceb633`, 2026-09-14) | the same 1.6.0 | 31 | 247 |

119 files were changed on both sides. A trial merge of `1.6.5rc` into `master` (in a throwaway
worktree, `git merge --no-commit`) stops on **42 files with 78 conflict hunks**: one file the
fork edited was deleted upstream (the Recent Books screen), six files were added on both sides
with different content (an OPF-parser test suite), and the rest are ordinary content
conflicts. Upstream `develop` is a further 55 commits past `1.6.5rc`; they are not part of
this feature.

The headline upstream change is the **Library**: a card-wide index of books with Recent /
Added / Title / Author tabs that replaces the Recent Books screen. It indexes `.epub`, `.txt`,
`.md` and `.xtc` files and does not know `.fb2` (`lib/LibraryIndex/LibraryBuilder.cpp:178-179`
at `1.6.5rc`). A plain merge would therefore leave every FictionBook on the card invisible in
the new Library, unreachable from Home's book list, and absent from the new
"Use book metadata" behaviour. That is the clearest case of what "applying the enhancements to
the fork's features" means; the others are smaller and listed below.

This feature is one sync, in three parts:

1. **Upgrade** — bring every `1.6.5rc` change into the fork, as a true merge, so that the
   next sync starts from `1.6.5rc` instead of re-resolving these conflicts.
2. **Keep** — nothing the fork added is lost: FictionBook 2 support with all of its
   sub-features, the stub cover sleep screen, the input-hardening and allocation fixes, the
   QA program (59 host suites, 3,349 tests, plain and sanitised), and the fork's tooling.
3. **Apply** — where an upstream enhancement has a fork-only counterpart, the counterpart
   gets it: FB2 books in the Library, the FB2 reader's chrome fixes, the FB2 and stub sleep
   covers, upstream's new test suites in the fork's program, and the fork's release process
   on upstream's new release pipeline.

Lazy shape, deliberately: one merge (no rebase, no cherry-pick reconstruction), conflicts
resolved toward upstream's shape wherever both sides solved the same problem
(Constitution VII), fork behaviour proven by the tests that already exist rather than by
new scaffolding, and new code only where an enhancement has to reach a fork-only feature.

### What upstream `1.6.5rc` brings (inventory A)

Derived from the 31 first-parent commits `54337e6d..a1ceb633`; the PR number is upstream's.

| Area | Change |
|---|---|
| Library | Library screen with Recent / Added / Title / Author tabs, search, group collapse; index of up to 4,096 books; settings **Use book metadata** and **Rebuild library index**; Recent Books screen removed (#3366, list back-navigation fix) |
| Reader | Ordered lists numbered, list containers indent (section cache v46, #3500); elements with the HTML `hidden` attribute not shown (#3390); reader menu shows the true current chapter position (#3437); end-of-book menu selection kept in sync (#3418); X3 anti-aliasing stabilised (#3439); absolute-grayscale image pages on UC8279 panels (#3478); font caches released before chapter layout (#3527); heap fragmentation reduced in the EPUB page model (#3518) and the font cache (#3521) |
| Files | NFD (macOS-transferred) Korean filenames render (#3036); web file page normalises paths and escapes names (#3353); static web pages served with ETag / Cache-Control (#2560); USB drive mode notices cable unplug (#3538) |
| Sync | KOSync uploads precise positions (#3174), compares mapped positions (#3111), memory checks fixed (#3412) |
| Input | Short presses register when the poll is idle (#3463); presses are not dropped while a list repaints (#3534) |
| Sleep | Sleep images skip the B/W pass; white is transparent on sleep covers (#3541) |
| Platform | pioarduino 55.03.311 with C3-specific sdkconfig (#3397); SdFat SPI transfers batched (#3501); font manifest packed into one arena (#3398); OTA assets renamed `crosspoint-<version>-<device>.bin`, built and attached on release publish, RCs included (#3493, `a1ceb633`); X4 Classic in CI (#3532); PSRAM subplot in the debug monitor (#3490); PR firmware download links (#3389) |
| Tests | 11 new host suites (`absolute_grayscale`, `chapter_position`, `fs_helpers`, `inflate_stream`, `koreader_xpath_resolver`, `library_builder`, `library_format`, `library_index_file`, `library_text`, `progress_comparison`, `sd_card_font`) and additions to 4 existing ones |

### What the fork must keep (inventory B)

Derived from the 242 fork commits `54337e6d..b68be959`.

| Area | Fork feature |
|---|---|
| FictionBook 2 | `.fb2` books open from the file browser, Home, the sleep screen, the next-book finder, the cache cleaner and the screenshot namer; windows-1251/1252 files; auxiliary bodies (notes, comments) excluded from chapters; every nested section a chapter with depth shown (specs/003); no chapter cap, chapter metadata on SD (specs/004, /006); untitled chapters labelled from their first line and the book opens on its cover (specs/004); the next chapter is laid out while the reader is idle, with resumable sliced builds (specs/005); body-level front matter carries progress weight (specs/007) |
| Sleep | Stub cover card (title and author, wrapped) when a book has no cover art (specs/002) |
| Hardening | Length and count fields validated before allocation in zip, `book.bin`, section files, FB2 caches, `.cpfont`, the TXT page index, CSS, JSON, PNG, JPEG, uzlib, KOSync xpointers and WebDAV paths; no bare `new` on any fallible path; web API refuses client paths that reach a protected item at any depth and masks the sync password |
| Performance | No `std::function` on render or download paths; no string allocation while a page renders; oversized buffers off task stacks; no redundant card rewrites; fewer per-word allocations in hyphenation; a platform seam that keeps library code host-compilable |
| UX fixes | Quick-return honoured when leaving a footnote; font-download errors translated; whole-framebuffer screenshots and a Screenshot action in the FB2 menu; FAT32 path components kept valid UTF-8; Markdown books cleared and covered like text books |
| QA program | 59 host suites / 3,349 tests (measured 2026-09-26, `bin/run-tests`, 100% pass, 5 by-design skips); `bin/run-tests` with `--asan`, `--quick`, `--filter`; `bin/run-tests-linux`; the malformed-input corpus; allocation-budget guards; the ASan+UBSan CI variant; `clang-format-fix -c`; the opt-in pre-push hook |
| Tooling & governance | spec-kit scaffolding, constitution, autocommit hooks; `bin/run-simulator` and its skill; `catalog-info.yaml`; dev version stamping on all five development environments; the `x4c-gh_release_rc` environment (upstream added the identical one; the trial merge keeps a single copy) |

## Clarifications

### Session 2026-09-27

- Q: When the sync PR lands on `master`, is it a merge commit, a squash (the fork's convention), or a rebase? → A: A merge commit; the squash convention does not apply to upstream syncs.
- Q: Does the fork adopt upstream's release workflow (CI attaches the assets on release publish), and how are fork releases named? → A: CI builds and attaches the assets. A fork release built on an upstream release candidate is named `<upstream version>rc-bb.N` (this sync: `1.6.5rc-bb.1`) and published as a GitHub pre-release; one built on an upstream final release is `<upstream version>-bb.N` and published as a normal release. The workflow's tag check is reduced to "tag equals the configured version" for both, since the fork's version already carries the `rc`.
- Q: How much on-device measurement before the sync ships — a three-run before/after bench, a single before/after run, or functional checks only? → A: One run before (on `1.6.0-bb.5`) and one after for each figure; any regression is explained by a named upstream change in the release notes or fixed.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A fork user gets everything 1.6.5rc has (Priority: P1)

A reader on `1.6.0-bb.5` installs the next fork release. Their Home screen now has a
**Library** instead of Recent Books; ordered lists in their EPUBs are numbered; the reader
menu shows the right page within the chapter; short presses register after the device has
been idle; on an X3 anti-aliased pages no longer smear; on an X4 Pro image pages use the
panel's absolute grayscale. Everything upstream shipped in `1.6.5rc` is there, unchanged.

**Why this priority**: This is the upgrade. Without it there is no feature.

**Independent Test**: Build the C3 firmware from the sync branch, flash an X4, and walk
inventory A row by row on the device (Library tabs, an EPUB with an ordered list, the reader
menu after returning from the chapter list, an idle-then-short-press, a sleep cover with a
white background). Separately, confirm with `git merge-base --is-ancestor 1.6.5rc HEAD`
that the upstream release is an ancestor of the branch.

**Acceptance Scenarios**:

1. **Given** the sync branch, **When** upstream tag `1.6.5rc` is checked for ancestry, **Then** it is an ancestor of the branch tip and no upstream commit was re-authored or dropped.
2. **Given** a card with EPUBs, **When** the reader opens Home, **Then** the menu offers Browse Files, Library, File Transfer and Settings, and the Library builds its index on first open and lists the EPUBs under all four tabs as described in upstream's user guide.
3. **Given** an EPUB whose section cache was built by `1.6.0-bb.5`, **When** it is opened, **Then** the chapter is laid out again (cache version 46) and ordered lists show their numbers; no crash, no stale layout.
4. **Given** the fork's build configuration after the merge, **When** the five firmware environments are built, **Then** each environment is defined exactly once, all five build, and the development environments still carry the branch-and-SHA version stamp.
5. **Given** any 1.6.5rc behaviour in inventory A, **When** it is exercised on the device or the simulator, **Then** it behaves as it does on stock upstream `1.6.5rc`.

---

### User Story 2 - Nothing the fork added is lost (Priority: P1)

The same reader still opens their FictionBook library: nested stories are chapters, a big
anthology opens on its cover, crossing a chapter boundary does not stall, the percentage
climbs through front matter. When the device sleeps on a coverless book, the stub card
still names it. Corrupt files still fail cleanly instead of crashing. The developer's
`bin/run-tests` still runs every suite the fork had, plain and sanitised, and they all pass.

**Why this priority**: "Keeping all the features in my fork" is half of the request, and the
fork's features are the reason the fork exists. A sync that loses one is a regression, not an
upgrade.

**Independent Test**: Run `bin/run-tests` and `bin/run-tests --asan` on the sync branch:
every suite and test that passes on `1.6.0-bb.5` still exists and passes. On the device,
run the quickstart checks of specs/002 through 007 on the reference FB2 books.

**Acceptance Scenarios**:

1. **Given** the sync branch, **When** the host test program runs plain and under ASan+UBSan, **Then** all 3,349 tests that pass on `1.6.0-bb.5` still exist and pass, with the same five by-design skips and no others; any test whose expectation changed is a pin flip that names the upstream change it follows.
2. **Given** the reference anthology (*Марсианские хроники*, 66 sections), **When** it is opened after the sync, **Then** the chapter list shows all 66 entries at their depths, the book opens on its cover, and the boundary into an unread chapter turns without an indexing popup after a normal dwell.
3. **Given** a book with more than 256 sections, **When** its chapter list is scrolled to the end, **Then** the last section is listed and opens.
4. **Given** the worst front-matter corpus book (256,971 bytes before its first section), **When** the reader turns pages through the front matter, **Then** the percentage moves at the same rate per page as in the rest of the book.
5. **Given** a book with no cover art and a cover sleep screen selected, **When** the device sleeps, **Then** the stub card shows the title and author, wrapped.
6. **Given** the malformed-input corpus, **When** every parser suite runs under the sanitiser, **Then** every file is rejected deterministically with no sanitiser report.
7. **Given** a web request naming a path under `/.crosspoint` at any depth, **When** it asks to list, delete, or create there, **Then** it is refused — the fork's every-component rule, not upstream's last-component rule.

---

### User Story 3 - Upstream's enhancements reach the fork's own features (Priority: P2)

The reader opens the new Library and their FictionBooks are there, under Title and Author
with the book's own title and author when **Use book metadata** is on, and they open into the
FB2 reader. In the FB2 reader the menu shows the true chapter position and the end-of-book
options stay in sync, as they now do for EPUB. A sleeping device shows an FB2 cover with the
same white-as-transparent treatment as an EPUB cover, and falls back to the stub card when
the cover cannot be produced. The developer's test program runs upstream's new suites too.

**Why this priority**: This is the "applying all the enhancements to them" half. It is
P2 only because it depends on the merge (P1) existing first; it is not optional.

**Independent Test**: On the device card (944 `.fb2` files as of 2026-09-22), open the
Library: the Title tab lists every FB2 on the card; open one from each tab. Toggle **Use
book metadata**, rebuild, and compare a row's title and author with the book's own. On the
host, `bin/run-tests` lists and passes upstream's 11 suites.

**Acceptance Scenarios**:

1. **Given** a card with `.fb2` files, **When** the Library index is built, **Then** every `.fb2` is indexed (up to the shared 4,096-book cap), counted like any other book, and shown under Recent, Added, Title and Author.
2. **Given** **Use book metadata** on, **When** the index is built, **Then** an FB2 row shows the title and author from the file's own metadata — the same fields the fork already reads for the Home tile and the sleep cover — and an FB2 that was opened before is not parsed again by the build (its existing metadata cache is reused, as the Library already does for EPUB); with the setting off, the row shows the filename stem.
3. **Given** an FB2 row in the Library, **When** it is selected, **Then** the FB2 reader opens it; holding it in Recent removes it from the list; deleting it clears its cache like any other book.
4. **Given** FB2 entries in the recent list before the sync, **When** the Library's Recent tab is opened after the sync, **Then** those entries still appear.
5. **Given** the FB2 reader, **When** the reader menu is opened after returning from the chapter list or the percent picker, **Then** the header shows the same chapter page as before leaving, following the same rule the EPUB reader now uses; **and** the end-of-book options keep the selection in sync; **and** rebuildable font caches are released before an FB2 chapter is laid out, as they now are before an EPUB chapter.
6. **Given** an X3 with anti-aliasing on, **When** FB2 pages are turned across a cleanup refresh, **Then** the display sequencing is the one the EPUB reader was stabilised to; no smeared page. (Verified by mechanism parity with upstream #3439 — no X3 is available to this fork; the release notes state the change is unverified on hardware.)
7. **Given** an FB2 book with an embedded cover, **When** the device sleeps on a cover sleep screen, **Then** the cover is rendered with the same threshold and transparency treatment as an EPUB cover; when the cover cannot be produced, the stub card is shown (the fork's fallback), not the generic sleep image.
8. **Given** the merged test tree, **When** `bin/run-tests` runs, **Then** upstream's 11 new suites are discovered and pass, plain and under ASan+UBSan; where a fork suite and an upstream suite cover the same code (`chapter_xpath_resolver` / `koreader_xpath_resolver`, `sdcard_font` / `sd_card_font`, `progress_mapper` / `progress_comparison`, `web_dav_paths` / `fs_helpers`, the two `content_opf_parser` trees), the union of their assertions is kept and no two suites share a target name.
9. **Given** an FB2 file with an NFD-composed Korean name, **When** the file browser lists it, **Then** its name renders composed, like any other file (format-agnostic upstream fixes apply to FB2 flows without further work — verified, not assumed).

---

### User Story 4 - Fork releases ride upstream's release pipeline (Priority: P2)

The maintainer bumps one version line, publishes a GitHub release tagged with that version,
and CI builds and attaches the five device binaries itself, named the way the firmware's
update check expects. No binaries are uploaded by hand.

**Why this priority**: Upstream rewrote the release workflow and the asset naming in this
range; the fork's hand-attached `firmware-<board>.bin` process is now the wrong shape. Riding
the new pipeline is both an enhancement to the fork's process and the only way the naming
stays coherent.

**Independent Test**: Publish `1.6.5rc-bb.1` as a pre-release and watch the release workflow attach
exactly five assets.

**Acceptance Scenarios**:

1. **Given** the version line reads `1.6.5rc-bb.1`, **When** a release tagged `1.6.5rc-bb.1` is published as a pre-release, **Then** the workflow builds all five environments and attaches `crosspoint-1.6.5rc-bb.1-x3-x4.bin`, `-sticky.bin`, `-x4pro.bin`, `-x4c.bin` and `-papermono.bin`, and nothing else.
2. **Given** a published fork release, **When** each asset is inspected, **Then** it contains the release tag as its version string (proof it came from the tagged commit).
4. **Given** a later fork release built on an upstream final release (for example `1.6.5-bb.1` on upstream `1.6.5`), **When** it is published as a normal release with the matching version line, **Then** the same workflow attaches its five assets with no edit.
3. **Given** the release notes, **When** they are read, **Then** they follow the fork's convention: measured figures against `1.6.0-bb.5` on the same device and card, and an explicit section for behaviour changes — Recent Books replaced by the Library, EPUB caches rebuilt once, and the release being based on an upstream release candidate.

---

### User Story 5 - Documentation and governance converge (Priority: P3)

Someone reading the fork's user guide finds the Library section and sees that `.fb2` is
among the indexed formats; the agent guide's facts (cache versions, screens, directories)
match the merged code; the constitution has been reviewed against the sync as it requires.

**Why this priority**: Stale documentation after a sync is how the next contributor — human
or agent — makes a confident wrong change. It is P3 because it does not block a reader.

**Independent Test**: Grep the user guide, README and agent guide for "Recent Books" as a
screen; read the agent guide's cache-version table against the code constants.

**Acceptance Scenarios**:

1. **Given** the merged `USER_GUIDE.md`, **When** the Library section is read, **Then** it is upstream's text with `.fb2` *(fork-only)* named among the indexed formats, and the old Recent Books section is gone.
2. **Given** the merged `docs/file-formats.md`, **When** it is read, **Then** both sides' additions are present: upstream's CLX1 library index and section version 46, and the fork's FB2, TXT and validation sections.
3. **Given** the agent guide (`AGENTS.md`, which `CLAUDE.md` links to), **When** its stated facts are checked against the merged code, **Then** the cache-version table, the list of screens and stores, the test counts and the platform version are correct.
4. **Given** the constitution's rule that every sync is reviewed for drift, **When** the plan's Constitution Check is written, **Then** it records that upstream's `AGENTS.md` and `SCOPE.md` are unchanged between 1.6.0 and 1.6.5rc (`git diff --stat` empty) and re-affirms the constitution explicitly.
5. **Given** the retrospective baseline spec (specs/001), **When** its Recent Books requirements are read, **Then** they carry a note that the Library (this spec) supersedes them, rather than describing a screen that no longer exists.

---

### Edge Cases

- **The same thing added on both sides.** Upstream and the fork both added the `x4c-gh_release_rc` environment and both added X4 Classic to CI; the trial merge kept single copies, but any duplicate section or matrix entry that does slip through must not survive to the build (a duplicated environment is a configuration error).
- **Same-named test suite, different content.** `test/content_opf_parser` exists on both sides with different files (six add/add conflicts). One suite results, with every assertion from both.
- **Pinned divergence.** Fork tests marked "documents current limitation" (`test/chapter_xpath_resolver`, `test/progress_mapper`) pin behaviour that upstream has since changed, each carrying the upstream-expected value in a comment. Where 1.6.5rc brings that change, the pin flips to the upstream expectation in the same commit; where 1.6.5rc does not, the pin stays.
- **A Library index built by stock upstream.** A card that was used with stock `1.6.5rc` holds an index without FB2 entries and the same format version, so nothing triggers a rebuild. The fork does not force one for this case: the user's **Rebuild library index** picks the FB2 books up, and the release notes say so.
- **Caches from `1.6.0-bb.5`.** EPUB section caches are rebuilt (v46). FB2, TXT and XTC caches either load unchanged or are rejected and rebuilt; none may crash the firmware (Constitution VI). The section-page layout is documented as unchanged in v46, so FB2 section caches are expected to load; if the shared page serialisation did change, the FB2 section version is bumped.
- **More than 4,096 books.** The Library's cap is upstream's; the device card (944 FB2 plus other formats) is under it. Books past the cap are absent from the Library but still open from Browse Files, as on stock upstream.
- **Very large FB2 files during an index build.** Reading an FB2's metadata for the index must stop at the end of the description block, never read the body; a build over the whole card must not exhaust the C3's heap.
- **Toolchain change.** The new pioarduino platform must install into the fork's pinned environment and build all five firmwares; the simulator must still build for the gate-6 check.
- **An abandoned or interrupted sync.** The sync branch is disposable; `master` is untouched until the PR lands. Re-running the merge from `master` yields the same 42 conflicts.
- **A pre-release flagged fork RC.** Upstream's workflow only attaches assets to a pre-release whose tag is `<configured version>rc`; the fork's `1.6.5rc-bb.1` carries its `rc` inside the version and would ship with no binaries under that rule — hence FR-031's single relaxation. Without it the failure is silent: the release exists, empty.
- **Landing.** The sync PR lands as a **merge commit**, not the fork's usual squash: a squash would erase the upstream ancestry that User Story 1 requires and force the next sync to re-resolve every conflict.

## Requirements *(mandatory)*

### Functional Requirements

**Upgrade**

- **FR-001**: Upstream tag `1.6.5rc` MUST be an ancestor of the sync branch tip and, after landing, of fork `master`: the sync is a merge, never a rebase, squash, or re-implementation of upstream commits.
- **FR-002**: The sync MUST land on `master` through a sync branch and pull request merged as a merge commit (`gh pr merge --merge`); the fork's squash convention MUST NOT be applied to this PR, and this holds for every future upstream sync.
- **FR-003**: Every behaviour in inventory A MUST be present and functional in the fork build, as it is in stock `1.6.5rc`.
- **FR-004**: Where both sides solved the same problem differently — web path normalisation, the smart-sync decision, font-manifest storage, dither-row validity checks, the file-handle accessors, the page model's ownership — the resolution MUST take upstream's shape (Constitution VII), and MUST keep each fork guarantee upstream's version lacks: (a) a protected item is refused at any path depth, not only as the last component; (b) no fallible allocation aborts — every one is null-checked; (c) a page whose cached image dimensions are impossible is rejected before it reaches the framebuffer; (d) manifest, asset and record sizes stay bounded before they drive an allocation.
- **FR-005**: The build configuration MUST define each environment exactly once; all five firmware environments MUST build; the fork's dev version stamping (branch and short SHA on every development environment) and the fork's sanitised CI test variant MUST survive the merge.
- **FR-006**: Caches written by `1.6.0-bb.5` MUST either load or be rejected and rebuilt; no pre-sync cache may crash the merged firmware.

**Keep**

- **FR-010**: Every feature in inventory B MUST remain present and behave as its spec (specs/002–007) or its originating fix describes.
- **FR-011**: Every host test that passes on `1.6.0-bb.5` MUST still exist and pass, plain and under ASan+UBSan, after the sync, with only the five by-design skips.
- **FR-012**: A fork test's expectation MAY change only when the behaviour it pins was deliberately changed by the sync; each such change MUST be a pin flip that names the upstream change it follows and MUST NOT be a deletion or skip added to make the run green. The one exception: tests of code the sync removes MAY be removed with it when the upstream code that replaces it ships its own coverage, and the commit names both.
- **FR-013**: The fork's tooling MUST keep working unchanged in use: `bin/run-tests` (all options), `bin/run-tests-linux`, `bin/run-simulator`, `./bin/clang-format-fix -c`, the pre-push hook installer, and the spec-kit autocommit hooks.

**Apply**

- **FR-020**: The Library MUST discover `.fb2` files and index them like any other supported book, under the same 4,096-book cap and the same folder, duplicate and unreadable-entry rules.
- **FR-021**: With **Use book metadata** on, an FB2 row's title and author MUST come from the file's own metadata (the fields the fork already reads for the Home tile and sleep cover), reusing an existing FB2 metadata cache rather than parsing again; with the setting off, the filename stem is used. A metadata read for the index MUST stop before the book's body.
- **FR-022**: FB2 rows MUST sort, group, fold and clean their author names by the same rules as other rows; selecting one MUST open the FB2 reader; removing from Recent and deleting with cache MUST work as for other formats.
- **FR-023**: FB2 entries recorded in the recent list before the sync MUST appear in the Library's Recent tab after it.
- **FR-024**: The FB2 reader MUST receive the reader-chrome enhancements upstream gave the EPUB reader in this range: the reader menu's current chapter position rule, end-of-book selection synchronisation, release of rebuildable font caches before chapter layout, and the X3 anti-aliased display sequencing; its chapter list MUST retain presses during a repaint (inherited from the shared list).
- **FR-025**: The FB2 cover sleep screen MUST use the same cover generation options and transparency treatment as the EPUB cover; when a cover cannot be produced for any format, the stub card MUST be shown (fork rule), not the generic sleep image.
- **FR-026**: Upstream's 11 new host suites MUST run in the fork's test program, plain and under ASan+UBSan, and pass; same-purpose suite pairs MUST be reconciled so that the union of their assertions is kept and no two suites register the same target name.
- **FR-027**: Format-agnostic upstream fixes (NFD filenames, list repaint presses, idle-poll wake, SD transfer batching) MUST be verified on an FB2 flow, not assumed to apply.

**Release**

- **FR-030**: Fork versions MUST follow `<upstream version>rc-bb.N` when built on an upstream release candidate and `<upstream version>-bb.N` when built on an upstream final release; this sync sets the configured version to `1.6.5rc-bb.1` (one line in the build configuration). Firmware assets MUST be named `crosspoint-<tag>-<device>.bin` with the five device names `x3-x4`, `sticky`, `x4pro`, `x4c`, `papermono`.
- **FR-031**: Publishing a GitHub release tagged with the configured version MUST build and attach all five assets automatically, whether the release is flagged pre-release (RC-based fork releases) or not (final-based ones): the release workflow's tag check MUST accept "tag equals configured version" in both cases — the one deliberate fork-only edit to upstream's workflow. The manual five-asset upload step is retired.
- **FR-032**: The release notes MUST follow the fork's convention (measured figures against `1.6.0-bb.5` on the same device and card; an explicit behaviour-change section naming the Library, the one-time EPUB cache rebuild, and the RC basis).

**Documentation and governance**

- **FR-040**: `USER_GUIDE.md` MUST carry upstream's Library section with `.fb2` *(fork-only)* named among indexed formats, and no longer describe a Recent Books screen; `README.md` MUST keep the fork's format line.
- **FR-041**: `docs/file-formats.md` MUST contain both sides' additions.
- **FR-042**: `AGENTS.md` MUST be corrected wherever the merged code changes a fact it states (cache-version table, screens and stores, the library index file, test counts, platform version). Edits are staged as `AGENTS.md`, since `CLAUDE.md` is a link to it.
- **FR-043**: The plan's Constitution Check MUST record the drift review this sync requires: upstream `AGENTS.md` and `SCOPE.md` are unchanged between 1.6.0 and 1.6.5rc, and the constitution is re-affirmed or amended accordingly.
- **FR-044**: specs/001's Recent Books requirements MUST be annotated as superseded by the Library, pointing at this spec.

### Key Entities

- **Upstream release 1.6.5rc**: the fixed target — one tag, 31 commits over 1.6.0; not `develop`.
- **Fork feature inventory (B)**: the set of behaviours the sync must preserve; each maps to a spec (002–007) or a fix commit, and to the host tests that pin it.
- **Library index entry**: one indexed book — path, size, title, author, folder, arrival order, metadata status; after this feature an entry may describe an FB2 book.
- **Book cache**: a per-book on-card cache with a format version; the sync changes the EPUB section version (45 → 46) and must leave the FB2, TXT and XTC versions coherent with their formats.
- **Release asset**: one of five per-device firmware binaries named `crosspoint-<version>-<device>.bin`, attached by the release workflow.
- **Host test program**: the union of the fork's 59 suites and upstream's 11 new ones after reconciliation, run plain and sanitised.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: `1.6.5rc` is an ancestor of fork `master` once the sync PR has landed (`git merge-base --is-ancestor` true), so the next upstream sync's trial merge reports none of the 42 conflicts resolved here.
- **SC-002**: The host test program passes 100% plain and under ASan+UBSan: every one of the 3,349 tests passing on `1.6.0-bb.5` still exists and passes (pin flips named in the plan excepted), every test in upstream's 11 new suites passes, exactly five tests are skipped (the by-design set), and at least 65 suites run (59 fork suites plus 11 upstream suites, less at most the 5 reconciled same-purpose pairs).
- **SC-003**: The sync PR's CI "Test Status" is green — format, cppcheck, all five firmware builds, both unit-test variants.
- **SC-004**: On the X4 with the device card, the Library's Title tab lists every `.fb2` on the card (the count matches a card scan), one FB2 opens from each of the four tabs into the FB2 reader, and the quickstart checks of specs/003–007 pass on the reference books.
- **SC-005**: Index build time on that card is measured with **Use book metadata** on and off and recorded in the release notes, alongside the number of books indexed.
- **SC-006**: Free heap at Home and with an FB2 open, the 24-row FB2 chapter-list window read, and the first open of the 677-chapter reference book are each measured once on `1.6.0-bb.5` and once on the sync build, same device and card, and both readings are published in the release notes; every figure that is worse after the sync is either attributed to a named upstream change in the notes or fixed before release. Pre-sync references from earlier sessions: 138,200 bytes free at Home (2026-09-20), 11 ms per window read and 2.9 s first open (specs/006 research R10).
- **SC-007**: Pre-release `1.6.5rc-bb.1` has exactly five assets, all attached by the workflow with zero manual uploads, and each asset contains the string `1.6.5rc-bb.1`.
- **SC-008**: No remaining reference to a top-level Recent Books screen in `USER_GUIDE.md`, `README.md` or `AGENTS.md`; the agent guide's cache-version table matches the code constants line for line.
- **SC-009**: All hand-written change in the sync beyond conflict resolution is confined to the "Apply" areas (Library ↔ FB2, FB2 reader chrome parity, sleep-cover parity, test-suite reconciliation) and documentation; the plan lists every such file.

## Assumptions

- "The origin" in the request means the upstream project, and the target is exactly tag `1.6.5rc` (`a1ceb633`), not `upstream/develop` (55 commits further as of 2026-09-26, including TrueType fonts on PSRAM boards, the Cover Grid theme, file rename, and "release SD-font caches on reader exit"). Those belong to a later sync.
- Fork tags carry no `v` prefix, like the rest of the `-bb` series; the version line and the tag are always identical, so the `rc` marker lives in the version itself (`1.6.5rc-bb.1`) rather than as a suffix the workflow appends.
- Upstream's release workflow replaces the fork's artifact-only workflow and the hand-attached `firmware-<board>.bin` assets, with one fork-only edit (FR-031). The firmware's own update check continues to point where it points today (upstream's releases); changing that is out of scope.
- The FB2 reader renders text only (its cover page is drawn black-and-white by design, `src/activities/reader/Fb2ReaderActivity.cpp:79`), so upstream's absolute-grayscale image-page path has nothing to reach in it; only the text anti-aliasing sequencing applies.
- Upstream's Library reads EPUB metadata by stopping the normal parser at the end of the metadata block; the FB2 equivalent stops at the end of the description block, which precedes the body in every FB2 file.
- Same-purpose test-suite pairs are kept as separate suites unless one is a strict subset of the other; the fork's runner discovers suites by directory, so upstream's suites are found without registration.
- The new pioarduino platform installs into the fork's pinned PlatformIO environment; if it does not, that is a plan-level blocker, not a scope change.
- Device measurements (SC-004 to SC-006) are taken on the X4 (ESP32-C3) with the card that held 944 `.fb2` files on 2026-09-22, using the fork's existing boot-time bench approach; the "before" readings are taken while `1.6.0-bb.5` is still on the device, before the sync build is flashed. Simulator checks cover the four orientations for the Library and FB2 screens.

## Out of Scope

- Upstream `develop` beyond `1.6.5rc`.
- Contributing FB2 support or any other fork feature upstream (separate, small PRs cut from `upstream/develop`).
- New fork features, new themes, and any change to the FB2 reader beyond parity with the EPUB reader's enhancements in this range.
- Changes to the sister simulator repository.
- Pointing the firmware's update check at the fork's own releases.

## Dependencies

- Upstream tag `1.6.5rc` and its submodule pin (the fork's `freeink-sdk` pin is an ancestor of upstream's, 50 commits behind; the submodule fast-forwards).
- An X4 device and the device card for SC-004 to SC-006; the fork's device bench approach for the timings.
- The fork's constitution v2.2.0, in particular Principles V (tests prove behaviour), VI (untrusted input) and VII (upstream-first hygiene), and its six quality gates.
