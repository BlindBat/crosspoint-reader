---
name: run-simulator
description: Build and launch the CrossPoint firmware in the desktop simulator from whatever is checked out right now, on a per-branch SD card, with the branch and SHA stamped into the firmware's version string. Use this whenever someone wants to see, run, try, open, preview or click through the firmware without a device — "run the simulator", "launch the sim", "let me see this on screen", "does this look right", "show me the reader/home screen/settings", "test the web UI in a browser", "check this UI change" — and also after any change to rendering, activities, input handling or the web server, where a host test suite proves logic but shows no pixels. Prefer this over a raw `pio run -e simulator`, which skips the toolchain setup, the per-branch SD card and the version stamping.
---

# Run the CrossPoint simulator

The simulator compiles `src/` natively and renders the e-ink panel in an SDL2
window, so rendering, input and activity flow can be exercised without hardware.
This skill wraps it so that what appears on screen is unambiguously the tree that
is checked out at that moment.

## Use it

```bash
bin/run-simulator
```

That builds the `simulator` env (X4), launches it detached, and prints the
branch, version, SD path, web-UI URL, PID and log path. Common variations:

```bash
bin/run-simulator sticky              # another board
bin/run-simulator --port 18080        # when 8080 is taken
bin/run-simulator --clean-cache       # after a layout/pagination change
bin/run-simulator --no-build          # relaunch the existing binary
bin/run-simulator --build-only        # compile-check without a window
bin/run-simulator --fg                # foreground, for interactive debugging
```

It sits in `bin/` alongside `run-tests` and `clang-format-fix` because it is an
ordinary developer command first — usable directly, with or without Claude. Both
it and this file are tracked on fork `master`; like the other fork gates, they
must not be carried into an upstream PR branch.

Boards: `x4` (default), `x3`, `pro`, `classic`, `sticky`, `papermono`, or a full
env name such as `simulator_x4_uc8279` for the panel variants.

Because it detaches by default, the script returns immediately and the window
stays up — run it from a normal Bash call and report the PID and log path back
to the user rather than blocking on it. Use `--fg` only when someone wants to
watch it in the terminal.

## What it does that a bare `pio run` does not

**Stamps the real version.** The `[env:simulator]` block in
`platformio.local.ini` hardcodes `CROSSPOINT_VERSION` to `dev-simulator`, and
`scripts/git_branch.py` skips the simulator (its `DEV_ENVS` covers only the
device envs), so an unwrapped build gives no clue which branch produced it. The
script redefines the macro at build time to
`<base>-dev-<branch>-<sha>[-dirty]`, matching the device format, so the boot log
and the About screen name the branch on screen. `-dirty` is appended when the
working tree has uncommitted changes, because the SHA alone would misdescribe
what is running.

**Gives each branch its own SD card.** Book caches key on the whole
`ReaderRenderSpec`, so a cache built before a layout change can be silently
reused after switching branches and show stale pagination. Each branch gets
`fs_branches/<branch>/`, with `books` and `fonts` symlinked to shared
`fs_branches/_books/` and `_fonts/` so large files are stored once.

**Sets up the toolchain.** PlatformIO's native platform resolves `gcc`/`g++` by
name from `PATH`; on macOS those are `/usr/bin` shims that refuse to run until
the Xcode licence is accepted. The script prefers `~/bin/xcshim` (creating it if
absent — symlinks to Xcode's clang, no `sudo`) and sources `build/hostenv.sh`
when present.

## Where things live

| What | Path |
|---|---|
| SD card root (`/` on device) | `fs_branches/<branch>/` |
| Books (`/books/`) | `fs_branches/_books/` — shared |
| Fonts (`/fonts/`) | `fs_branches/_fonts/` — shared |
| Settings, progress, caches | `fs_branches/<branch>/.crosspoint/` |
| Binary | `.pio/build/<env>/program` |
| Log | `$TMPDIR/crosspoint-sim-<branch>-<env>.log` |

The repo's `.gitignore` covers the simulator's default `fs_` but not this tree,
so the script drops a `.gitignore` containing `*` inside `fs_branches/` on first
run — the folder ignores itself and never shows up in `git status`, without
touching a tracked file. Drop `.epub` files into `fs_branches/_books/` and every
branch sees them; `scripts/generate_test_epub.py` makes fixtures if there is
nothing to hand.

## Driving it

Keyboard, from `crosspoint-simulator/src/HalGPIO.cpp`:

| Key | Button |
|---|---|
| `Esc` | Back |
| `Enter` | Confirm |
| `←` `→` | Left / Right |
| `↑` `↓` | Up / Down |
| `P` | Power |
| `H` | Home key (hold ~700 ms for long-press) |
| `S` | Sleep |

Mouse click and drag synthesise touch on touch-capable boards.

In list screens `←`/`→` behave as `↑`/`↓`: `ButtonNavigator` asks only for
`NavNext`/`NavPrevious`, which `MappedInputManager` resolves to *side Down or
front Right* and *side Up or front Left*. That is by design, so the front row can
drive lists. The arrows separate in the reader, keyboard entry, the percent and
interval pickers, the frontlight panel and the OPDS browser. In `INVERTED` and
`LANDSCAPE_CCW` the nav axis flips, matching the rotated hints.

## Web UI in a browser

The firmware's own web server runs on the host. It binds only once **File
Transfer** is started from the Home menu — nothing listens before that. Then open
`http://127.0.0.1:8080/` (or the `--port` value; WebSocket uses the next port
up). Uploads, streamed downloads and WebDAV verbs all work.

## When it misbehaves

**Stale layout after switching branches** — `--clean-cache` drops book caches and
keeps settings; `--reset-sd` wipes the branch's card entirely.

**`[env:simulator] is not defined`** — `platformio.local.ini` is gitignored and
per-developer. Start from `crosspoint-simulator/sample-platformio-macos.ini`.

**Build errors inside `crosspoint-simulator/src/`** — the simulator stubs the
HAL, so a firmware change that adds a HAL method or alters an SDK signature needs
the stub updated to match. That is a fix in the simulator repo, not this one.

**Immediate exit** — the script prints the tail of the log. A missing SDL2
(`brew install sdl2`) is the usual cause.

**Xcode licence errors despite the shim** — something else on `PATH` is winning.
`sudo xcodebuild -license` fixes it permanently.

## Scope

This covers rendering, input and activity flow. E-ink timing and ghosting, sleep
and power behaviour, and real heap-pressure claims still need hardware with
serial output — the simulator's heap telemetry is a host allocator, not a 380 KB
device.
