# Syncing the fork with an upstream tag

What made the 1.6.5rc sync (PR #21, 2026-09-27) land clean, in the order that avoids the round trips it cost.

1. **Harvest upstream's post-tag CI fixes before pushing.** `git log <tag>..upstream/master -- .github/workflows platformio.ini`.
   The 1.6.5rc CI failed `default`/`sticky` for a bug upstream had already fixed after the tag (the pioarduino penv pin, #3594) —
   one 15-minute cycle. Cherry-pick with `-x` so the upstream author stays the author.
2. **Format the whole tree, not `-g`.** `./bin/clang-format-fix -c`: merge-resolved files are staged, so `-g` skips them.
   `pio check` runs Cppcheck 2.20 and fails the gate on `[low:style]` too.
3. **Resolve conflicts toward upstream's shape**; keep the fork guarantees by grep: protected web paths, nothrow `new`, the
   page-image edge cap, manifest caps and crc32, the size_t-safe release parser (the audit greps are in specs/008, T027).
4. **Test program:** upstream identity stubs (`test/*/stubs/FsHelpers.h`, `Serialization.h`) shadow real code — `git rm` them.
   Flip a pinned expectation only for an upstream behaviour change, and name the upstream PR in the test comment.
5. **Gates before the PR:** `./bin/clang-format-fix -c`, `bin/run-tests`, `bin/run-tests --asan`, all five `pio run` environments,
   `pio check`. `pio check` deletes `default`'s build products — read `firmware.bin` (version `strings`) before it.
6. **Land as a merge commit.** `gh pr create -R BlindBat/crosspoint-reader --head BlindBat:<branch>` (without `-R` and the
   owner prefix `gh` resolves the head against the upstream parent and fails); `gh pr merge --merge --delete-branch`, never squash.
7. **Release.** `gh release create <version> --prerelease --target master --notes-file …` for an rc basis (`<upstream>rc-bb.N`),
   without `--prerelease` for a final one (`<upstream>-bb.N`). The workflow builds and attaches the five
   `crosspoint-<version>-<device>.bin` on `release: published`; a tag push alone builds nothing. Check with `strings` that each
   asset carries the version.

Device figures for the release notes (heap at Home and with an FB2 open, chapter-list window read, first open of the
reference book, Library index build) come from a throwaway boot-time bench captured with `scripts/flash_and_capture.sh`.
