# Phase 1 Quickstart: Validating the Stub Cover Sleep Screen

The card has no host-testable surface (see [research.md](research.md) §4), so validation is
visual, in the simulator, per constitution gate 6. This recipe was used to verify the
retrospective half of the feature and is reproducible for the wrapping enhancement.

## Prerequisites

- The simulator wired up per `.claude/skills/run-simulator/SKILL.md` (`bin/run-simulator`).
- A book with **no cover art**. Any EPUB whose OPF declares no cover item will do; the generator
  below makes one with a deliberately long title and author for the wrapping checks.

## 1. Make a coverless fixture

```bash
python3 - <<'PY'
import zipfile
out = "fs_branches/_books/No Cover Test.epub"
opf = '''<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="bid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="bid">urn:uuid:nocover-0001</dc:identifier>
    <dc:title>A Book Without Any Cover Art At All, Volume Two of the Collected Editions</dc:title>
    <dc:creator>Jean-Luc Placeholder</dc:creator>
    <dc:language>en</dc:language>
  </metadata>
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/>
  </manifest>
  <spine><itemref idref="c1"/></spine>
</package>'''
nav = '<?xml version="1.0" encoding="utf-8"?>\n<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Nav</title></head><body><nav epub:type="toc"><ol><li><a href="c1.xhtml">One</a></li></ol></nav></body></html>'
c1 = '<?xml version="1.0" encoding="utf-8"?>\n<html xmlns="http://www.w3.org/1999/xhtml"><head><title>One</title></head><body><h1>Chapter One</h1>' + "".join(f"<p>Paragraph {i}.</p>" for i in range(1, 60)) + '</body></html>'
container = '<?xml version="1.0"?>\n<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>'
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("mimetype", "application/epub+zip", zipfile.ZIP_STORED)
    z.writestr("META-INF/container.xml", container)
    z.writestr("OEBPS/content.opf", opf)
    z.writestr("OEBPS/nav.xhtml", nav)
    z.writestr("OEBPS/c1.xhtml", c1)
print("wrote", out)
PY
```

## 2. Point the branch's SD card at it

The card lives at `fs_branches/<branch>/.crosspoint/`. **Back `settings.json` up first** — this
step edits it, and the caches under `.crosspoint/epub_*` hold reading progress.

```bash
SD=fs_branches/$(git branch --show-current)/.crosspoint
cp "$SD/settings.json" /tmp/cp-settings.bak
python3 - <<'PY'
import json, pathlib, subprocess
branch = subprocess.check_output(["git","branch","--show-current"], text=True).strip()
sd = pathlib.Path("fs_branches")/branch/".crosspoint"
s = json.loads((sd/"settings.json").read_text()); s["sleepScreen"] = 3  # Cover
(sd/"settings.json").write_text(json.dumps(s, ensure_ascii=False))
t = json.loads((sd/"state.json").read_text())
t["openEpubPath"] = "/books/No Cover Test.epub"; t["showBootScreen"] = False
(sd/"state.json").write_text(json.dumps(t, ensure_ascii=False))
PY
```

`sleepScreen: 3` is Cover. Mode 4 (Cover + Custom) also works but only reaches the card when
`lastSleepFromReader` is true — set that too if you are checking FR-006.

## 3. Drive it and capture the screen

The simulator takes a scripted input timeline and a screenshot schedule from the environment
(`crosspoint-simulator/src/HalGPIO.cpp`, `HalDisplay.cpp`), so no manual key presses are needed:

```bash
OUT=/tmp/stub-cover.bmp
CROSSPOINT_SIM_INPUT_SCRIPT="2500:S;9000:QUIT" \
CROSSPOINT_SIM_SCREENSHOTS="6000:$OUT" \
  bin/run-simulator --fg 2>&1 | grep -i "SLP\|screenshot"
```

`2500:S` sleeps at 2.5 s (the `S` key drives the real power-hold sleep path), the screenshot
lands at 6 s, and the run quits at 9 s.

**Expected log**:

```text
[SLP] Failed to generate cover bmp
[SLP] No cover art, rendering stub cover
[SIM] Saved screenshot: /tmp/stub-cover.bmp
```

## 4. Check the screen against the spec

Open the BMP. It should show a double-line frame with the title in bold above the author.

| Check | Requirement |
|---|---|
| The book is named on screen at all | FR-001, SC-001 |
| The long title occupies more than one line and ends without an ellipsis | FR-009, SC-003 |
| The author is on its own line(s), visibly smaller than the title | FR-008, FR-010 |
| Neither text touches or crosses either frame line | FR-011, FR-012 |
| The title-to-author gap is unchanged from the one-line case | FR-012 |
| Exactly one screen update in the log, no second flash | FR-014, SC-004 |

Then vary the fixture: a one-word title (single line, unchanged from today), a title of ~300
characters (ellipsis on the last line, nothing clipped), a title with no spaces (ellipsised, no
overflow), `dc:creator` removed (title-only card, still balanced), and a Cyrillic or Arabic title
(script renders, RTL keeps its direction).

## 5. Also confirm what must NOT change

- A book **with** cover art still shows the art (FR-002 negative case, SC-007).
- `sleepScreen` set to Dark, Light, Custom, None, Quick Resume or Transparent is untouched.
- A book path that no longer exists still falls back to the default screen — no card, no crash.

## 6. Restore the card

```bash
cp /tmp/cp-settings.bak "fs_branches/$(git branch --show-current)/.crosspoint/settings.json"
rm -f "fs_branches/_books/No Cover Test.epub"
```

Delete only the fixture's own cache directory if you clear caches at all —
`rm -rf .crosspoint/epub_*` takes every book's `progress.bin` with it.

## 7. Gates

The simulator is gate 6, not a substitute for the rest. Before merging:

```bash
./bin/clang-format-fix -g
bin/run-tests
bin/run-tests --asan
pio run -e default
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
```

E-ink refresh behaviour (the single half refresh, ghosting) is the one claim the simulator cannot
settle; confirm it on hardware with serial output.
