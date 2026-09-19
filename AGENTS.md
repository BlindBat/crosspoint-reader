# CrossPoint Reader Development Guide

Project: Open-source e-reader firmware for the Xteink X4/X3 (ESP32-C3) and the ESP32-S3 boards Seeed Sticky, Xteink X4 Pro, Xteink X4 Classic and M5Stack Paper Mono
Mission: Provide a lightweight, high-performance reading experience focused on EPUB rendering on constrained hardware.

## AI Agent Identity and Cognitive Rules

* Role: Senior Embedded Systems Engineer (ESP-IDF/Arduino-ESP32 specialized).
* Primary Constraint: 380KB RAM is the hard ceiling. Stability is non-negotiable.
* Evidence-Based Reasoning: Before proposing a change, you MUST cite the specific file path and line numbers that justify the modification.
* Anti-Hallucination: Do not assume the existence of libraries or ESP-IDF functions. If you are unsure of an API's availability for the ESP32-C3 RISC-V target, check the freeink-sdk source or the FreeInk SDK docs (https://freeink.org/llms.txt for an LLM-readable index) first.
* No Unfounded Claims: Do not claim performance gains or memory savings without explaining the technical mechanism (e.g., DRAM vs IRAM usage).
* Resource Justification: You must justify any new heap allocation (new, malloc, std::vector) or explain why a stack/static alternative was rejected.
* Verification: After suggesting a fix, instruct the user on how to verify it (e.g., monitoring heap via Serial or checking a specific cache file).

---

## Development Environment Awareness

**CRITICAL**: Detect the host platform at session start to choose appropriate tools and commands.

### Platform Detection

```bash
# Detect platform (run once per session)
uname -s
# Returns: MINGW64_NT-* (Windows Git Bash), Linux, Darwin (macOS)
```

**Detection Required**: Run `uname -s` at session start to determine platform

### Platform-Specific Behaviors

- **Windows (Git Bash)**: Unix commands, `C:\` paths in Windows but `/` in bash, limited glob (use `find`+`xargs`)
- **Linux/WSL**: Full bash, Unix paths, native glob support

**Cross-Platform Code Formatting**:

```bash
./bin/clang-format-fix -g
```

Never invoke or probe `clang-format` directly. The repository wrapper is the only sanctioned entry point.

---

## Platform and Hardware Constraints

### Hardware Specs

* MCUs: ESP32-C3 (single-core RISC-V @ 160MHz; X4 and X3, one binary, runtime-detected) and ESP32-S3 (dual-core Xtensa LX7; `sticky`, `x4pro`, `x4c`, `papermono`)
* RAM: ~380KB usable on ESP32-C3 (VERY LIMITED - primary project constraint)
  * **NO PSRAM on C3**. `sticky` also runs without PSRAM (the 48KB framebuffer fits in DRAM); `x4pro`, `x4c` and `papermono` build with `BOARD_HAS_PSRAM`.
  * **Single Buffer Mode**: Only ONE 48KB framebuffer (not double-buffered)
* Flash: 16MB (Instruction storage and static data)
* Display: 800x480 E-Ink (Slow refresh, monochrome, 1-2s full update)
  * Framebuffer: 48,000 bytes (800 × 480 ÷ 8)
* Storage: SD Card (Used for books and aggressive caching)

### The Resource Protocol

1. Stack Safety: Limit local function variables to < 256 bytes. The ESP32-C3 default stack is small; use std::unique_ptr or static pools for larger buffers.
2. Heap Fragmentation: Avoid repeated new/delete in loops. Allocate buffers once during onEnter() and reuse them.
3. Flash Persistence: Large constant data (UI strings, lookup tables) MUST be marked static const to stay in Flash (Instruction Bus), freeing DRAM.
4. String Policy: Prohibit std::string and Arduino String in hot paths. Use std::string_view for read-only access and snprintf with fixed char[] buffers for construction.
5. UI Strings: All user-facing text must use the `tr()` macro (e.g., `tr(STR_LOADING)`) for i18n support. Never hardcode UI strings directly. For the avoidance of doubt, logging messages (LOG_DBG/LOG_ERR) can be hardcoded, but user-facing text must use `tr()`.
6. `constexpr` First: Compile-time constants and lookup tables must be `constexpr`, not just `static const`. This moves computation to compile time, enables dead-branch elimination, and guarantees flash placement. Use `static constexpr` for class-level constants.
7. `std::vector` Pre-allocation: Always call `.reserve(N)` before any `push_back()` loop. Each growth event allocates a new block (2×), copies all elements, then frees the old one — three heap operations that fragment DRAM. When the final size is unknown, estimate conservatively.
8. SD Persistence Throttling: Settings, state, credentials, and other `PersistableStore` JSON files live on SD under `/.crosspoint/` through `HalStorage`; SPIFFS is not mounted. Guard redundant writes and debounce progress saves to avoid serialization, SD I/O, and `storageMutex` cost.
9. `new` is not nothrow on ESP32: With `-fno-exceptions`, bare `new` that fails calls `abort()` — it does NOT return `nullptr`. Always use `new (std::nothrow)` and null-check the result, or use `makeUniqueNoThrow<T>()` from `lib/Memory/Memory.h`. Never write bare `new` for any fallible allocation.

---

## Project Architecture

### Build System: PlatformIO

**PlatformIO is BOTH a VS Code extension AND a CLI tool**:

1. **VS Code Extension** (Recommended):
   
   * Extension ID: `platformio.platformio-ide`
   
   * Provides: Toolbar buttons, IntelliSense, integrated build/upload/monitor
   
   * Configuration: `.vscode/` is gitignored, so any editor config is local-only and not part of the repo
   
   * Usage: Click Build (✓), Upload (→), or Monitor (🔌) buttons

2. **CLI Tool** (`pio` command):
   
   * **Installation**: Python package (typically `pip install platformio`)
   
   * **Windows Location**: `C:\Users\<user>\AppData\Local\Programs\Python\Python3xx\Scripts\pio.exe`
   
   * **Verify**: `which pio` (Git Bash) or `where.exe pio` (cmd)
   
   * **Usage**: `pio run`, `pio run -t upload`, etc.

**Configuration Files**:

* `platformio.ini`: Main build configuration (committed to git)
* `platformio.local.ini`: Local overrides (gitignored, create if needed)
* `partitions.csv`: ESP32 flash partition layout

### Build Environment

* **Standard**: C++20 (`-std=gnu++2a`, with `-std=gnu++11` in `build_unflags`). No exceptions: `-fno-exceptions` is set and `-fexceptions` unflagged.
* **Logging**: ALWAYS use `LOG_INF`, `LOG_DBG`, or `LOG_ERR` from `Logging.h`. Raw Serial output is deprecated.
* **Environments** (in `platformio.ini`): one binary per MCU family, so every S3 board has its own environment.
  * **ESP32-C3** (`FREEINK_DEVICE_X4` + `FREEINK_DEVICE_X3`, one binary for both, runtime-detected):
    * `default`: Development (LOG_LEVEL=2, serial enabled)
    * `gh_release`: Production (LOG_LEVEL=1)
    * `gh_release_rc`: Release candidate (LOG_LEVEL=1)
    * `slim`: Minimal build (`-UENABLE_SERIAL_LOG`, so no logging is compiled in at all)
  * **ESP32-S3**: `sticky` (Seeed Sticky), `x4pro` (Xteink X4 Pro), `x4c` (Xteink X4 Classic) and `papermono` (M5Stack Paper Mono). Each is a development environment (LOG_LEVEL=2) and each has `-gh_release` and `-gh_release_rc` variants (e.g. `sticky-gh_release`, `x4c-gh_release_rc`) at LOG_LEVEL=1.
  * `LOG_LEVEL` semantics: 0 = ERR only, 1 = ERR + INF, 2 = ERR + INF + DBG; unset defaults to 0 ([lib/Logging/Logging.h:15-32](lib/Logging/Logging.h)). Logging is only compiled in when `ENABLE_SERIAL_LOG` is defined.
  * `scripts/git_branch.py` injects a `<version>-dev-<branch>-<sha>` `CROSSPOINT_VERSION` for the five environments in `DEV_ENVS` — `default`, `sticky`, `x4pro`, `x4c`, `papermono` ([scripts/git_branch.py:81](scripts/git_branch.py)). Every other environment takes the `CROSSPOINT_VERSION` set in `platformio.ini`. The development environments deliberately carry no `CROSSPOINT_VERSION` in the ini, so the script's value is the only definition.

### Critical Build Flags

These flags in `platformio.ini` fundamentally affect firmware behavior:

```cpp
-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1  // Single framebuffer (saves 48KB RAM!)
-DARDUINO_USB_MODE=1                 // Enable USB CDC
-DARDUINO_USB_CDC_ON_BOOT=1          // Serial available immediately at boot
-DXML_CONTEXT_BYTES=1024             // XML parser memory limit (EPUB parsing)
-DUSE_UTF8_LONG_NAMES=1              // SD card long filename support
-DXML_GE=0                           // Disable XML general entities (security)
-DDESTRUCTOR_CLOSES_FILE=1           // FsFile destructor auto-closes (SdFat)
-DFREEINK_CAP_USB_MSC=1              // USB Mass Storage (x4pro / x4c / papermono only)
-DBOARD_HAS_PSRAM                    // S3 boards with PSRAM (x4pro / x4c / papermono)
-DUSE_BLOCK_DEVICE_INTERFACE=1       // Native 1-bit SDMMC via SdFat (x4pro / x4c / papermono)
```

**Not a build flag**: miniz is configured by a header, not `platformio.ini`. `MINIZ_NO_ZLIB_COMPATIBLE_NAMES` — along with `MINIZ_NO_STDIO`, `MINIZ_NO_TIME`, `MINIZ_NO_ARCHIVE_APIS`, `MINIZ_NO_ARCHIVE_WRITING_APIS` and `MINIZ_NO_DEFLATE_APIS` — is defined in [lib/miniz/src/MinizConfig.h:8-13](lib/miniz/src/MinizConfig.h). Include `MinizConfig.h` rather than `<miniz.h>` so every translation unit sees the same configuration; the same header renames `tinfl_*` to `crosspoint_tinfl_*` so the linker cannot silently bind to the ESP32 mask ROM's incompatible copy ([lib/miniz/src/MinizConfig.h:15-25](lib/miniz/src/MinizConfig.h)).

**DESTRUCTOR_CLOSES_FILE implications**:

- SdFat's `FsBaseFile` destructor calls `close()` automatically when the object goes out of scope
- **Do NOT add explicit `file.close()` calls** for local `FsFile` variables — the destructor handles it
- Explicit `close()` is still required in these cases:
  
  1. **Close before delete**: Must close before `Storage.remove()` on the same path
  
  2. **Close before reopen**: Must close before reopening the same `FsFile` variable (e.g., write then reopen for read, or rewrite the same path)
  
  3. **Member variables**: `FsFile` members persist beyond any single function scope, so close at the intended release point (e.g., in `onExit()`)

**SINGLE_BUFFER_MODE implications**:

- Only ONE framebuffer exists (not double-buffered)
- Grayscale (anti-aliased) rendering therefore has **two distinct paths**, and they are not interchangeable:
  
  1. **Strip (tiled) grayscale** — gated by `renderer.supportsStripGrayscale()`. Each grey plane is rendered one horizontal band at a time into a caller-owned scratch strip via `beginStripTarget()` / `endStripTarget()` and streamed straight to controller RAM with `writeGrayscalePlaneStrip()`. The BW framebuffer is never overwritten, so **no snapshot is taken and `storeBwBuffer()` is not called**. The reader uses 80-row bands. See `GfxRenderer::beginStripTarget` and the tiled branch of `EpubReaderActivity`'s page render.
  
  2. **Snapshot grayscale** — every other panel. `renderer.storeBwBuffer()` copies the framebuffer into heap chunks of `BW_BUFFER_CHUNK_SIZE` (8,000 bytes) so no 48KB *contiguous* block is needed; the grey planes are then rendered over the framebuffer, and `renderer.restoreBwBuffer()` copies it back and frees the chunks. `storeBwBuffer()` returns **false** on allocation failure — callers must skip anti-aliasing for that page rather than proceed.
- `restoreBwBuffer(resyncPanelBaseline = true)` also rewrites the controller's differential baseline. Pass `false` when the glass shows content painted *after* the snapshot (overlay chrome), or the next differential update will leave that content on screen.
- Raw `malloc`/`free` inside `GfxRenderer` is confined to `drawBitmap()` / `drawBitmap1Bit()` (scaling row buffers), `fillPolygon()` (scanline node array) and `storeBwBuffer()` (the BW snapshot chunks). Everything else uses `makeUniqueNoThrow`.

### Directory Structure

* lib/: Internal libraries (Epub engine, GfxRenderer, UITheme, I18n)
  * lib/hal/: Hardware Abstraction Layer (HalClock, HalDisplay, HalFrontlight, HalGPIO, HalPowerManager, HalStorage, HalSystem, HalTiltSensor)
  * lib/I18n/: Internationalization (translations in `translations/*.yaml`, generated string tables)
* src/activities/: UI logic using the Activity Lifecycle (onEnter, loop, onExit)
* freeink-sdk/: Low-level SDK (EInkDisplay, InputManager, BatteryMonitor, SDCardManager)
* .crosspoint/: SD-based store for the `PersistableStore` JSON files (`settings.json`, `state.json`, `wifi.json`, `opds.json`, `recent.json`, `koreader.json`) and the per-book binary caches (`epub_`/`fb2_`/`txt_`/`xtc_` prefixed)

### Hardware Abstraction Layer (HAL)

**CRITICAL**: Always use HAL classes, NOT SDK classes directly.

| HAL Class         | Wraps SDK Class                 | Purpose                                            | Singleton access          |
| ----------------- | ------------------------------- | -------------------------------------------------- | ------------------------- |
| `HalClock`        | `Rtc`                           | RTC read/format, NTP sync                          | `halClock` (extern)       |
| `HalDisplay`      | `EInkDisplay`                   | E-ink display control                              | `display` (extern)        |
| `HalFrontlight`   | `FrontlightManager`             | Frontlight brightness / warmth                     | `Frontlight` (macro)      |
| `HalGPIO`         | `InputManager`                  | Button input handling, pin map                     | `gpio` (extern)           |
| `HalPowerManager` | `BatteryMonitor`, `InputManager`| CPU frequency, battery %, deep sleep               | `powerManager` (extern)   |
| `HalStorage`      | `SDCardManager`, `UsbMassStorage` | SD card file I/O (+ USB MSC where supported)      | `Storage` (macro)         |
| `HalSystem`       | *(ESP panic handler + SD)*      | Panic capture/dump, reboot-from-panic detection    | *(free functions in the `HalSystem` namespace)* |
| `HalTiltSensor`   | `Imu`                           | Tilt gesture detection for page turns              | `halTiltSensor` (extern)  |

**Location**: [lib/hal/](lib/hal/)

Several of these are **board-gated**. `HalFrontlight` reports `present() == false` and is inert on boards without a frontlight; `HalClock` and `HalTiltSensor` expose `isAvailable()` and must be checked before use; `HalStorage`'s USB Mass Storage path is compiled only under `FREEINK_CAP_USB_MSC`. Check `BoardConfig` capabilities / `FREEINK_DEVICE_*` before assuming a feature exists on every board.

**Why HAL?**

- Provides consistent error logging per module
- Abstracts SDK implementation details
- Centralizes resource management

**Example - HalStorage**:

```cpp
#include <HalStorage.h>

// Use Storage singleton (defined via macro)
HalFile file;
if (Storage.openFileForRead("MODULE", "/path/to/file.bin", file)) {
  // Read from file
  // No file.close() needed — DESTRUCTOR_CLOSES_FILE=1 handles it at scope exit
}
```

**Usage**: Use `HalFile` (the mutex-wrapping handle), NOT raw SdFat `FsFile` or Arduino `File`. Do NOT add `file.close()` for local variables (see DESTRUCTOR_CLOSES_FILE above).

**SdFat is not thread-safe; all SD access MUST go through HalStorage**:

- SdFat's `SdSpiCard` tracks SPI bus state with an unsynchronized `m_spiActive` bool. Two tasks calling SdFat concurrently can confuse that state machine and end with one task calling `SPIClass::endTransaction()` against a paramLock the *other* task is holding. That trips FreeRTOS's `xTaskPriorityDisinherit` assert (`tasks.c:5156, pxTCB == pxCurrentTCBs[0]`) and panics the system. See SdFat issue #518.
- `HalStorage` serializes everything via `storageMutex`, a **recursive** mutex so the same task can re-enter `StorageLock` without self-deadlock. Downstream code uses `HalFile` (declared in `<HalStorage.h>`); every method that touches the card — `read`, `write`, `seek*`, `available`, `position`, `rename`, `flush`, `getName`, `close`, `openNextFile`, `rewindDirectory` — takes the mutex. `HalFile`'s destructor also takes the mutex before letting the underlying SdFat `FsFile` close.
- **Exception — lock-free accessors**: `size()`, `fileSize()`, `fileSize64()`, `isDirectory()`, `isOpen()` and `operator bool()` forward to SdFat *without* taking the mutex. They only read state already cached in memory, so they are safe to call from any task and cheap in hot loops ([lib/hal/HalStorage.cpp:271-273, 286-288, 305-306](lib/hal/HalStorage.cpp)). Do not add locking around them, and do not assume any other accessor is free.
- A default-constructed or moved-from `HalFile` has no `Impl`; every accessor built on `HAL_FILE_GUARD` then logs `LOG_ERR` and returns a fail value rather than dereferencing, so a bad handle degrades instead of aborting on device ([lib/hal/HalStorage.cpp:247-251](lib/hal/HalStorage.cpp)). The hand-written `flush()`, `rewindDirectory()` and `isOpen()` return silently instead.
- **Never** call into `SdFat` / `SdSpiCard` / `FsBaseFile` / `SDCardManager` / raw `FsFile` directly — that bypasses the mutex.

---

## Coding Standards

### Naming Conventions

* Classes: PascalCase (e.g., EpubReaderActivity)
* Methods/Variables: camelCase (e.g., renderPage())
* Constants: UPPER_SNAKE_CASE (e.g., MAX_BUFFER_SIZE)
* Private Members: memberVariable (no prefix)
* File Names: Match Class names (e.g., EpubReaderActivity.cpp)

### Header Guards

* Use #pragma once for all header files.

### Comment Style

* Keep comments short and write them for the merged state, as if the code had always worked this way.
* Remove before/after narration, investigation measurements, and rationale that belongs in the commit message.
* Keep only non-obvious mechanism, field/parameter meaning, or the reason a special case exists.

### Memory Safety and RAII

* Smart Pointers: Prefer std::unique_ptr. 
* RAII: Use destructors for cleanup. Call `vTaskDelete()` explicitly for deterministic task release. Do NOT call `file.close()` on local `FsFile` variables — `DESTRUCTOR_CLOSES_FILE=1` handles it at scope exit (see Critical Build Flags).

### ESP32-C3 Platform Pitfalls

#### `std::string_view` and Null Termination

`string_view` is *not* null-terminated. Passing `.data()` to any C-style API (`drawText`, `snprintf`, `strcmp`, SdFat file paths) is undefined behaviour when the view is a substring or a view of a non-null-terminated buffer.

**Rule**: `string_view` is safe only when passing to C++ APIs that accept `string_view`. For any C API boundary, convert explicitly:

```cpp
// WRONG - undefined behaviour if view is a substring:
renderer.drawText(font, x, y, myView.data(), true);

// CORRECT - guaranteed null-terminated:
renderer.drawText(font, x, y, std::string(myView).c_str(), true);

// CORRECT - for short strings, use a stack buffer:
char buf[64];
snprintf(buf, sizeof(buf), "%.*s", (int)myView.size(), myView.data());
```

#### `IRAM_ATTR` and Flash Cache Safety

All code runs from flash via the instruction cache. During internal-flash operations such as OTA writes or NVS updates, the cache is briefly suspended. Any code that can execute during this window — ISRs in particular — must reside in IRAM or it will crash silently.

```cpp
// ISR handler: must be in IRAM
void IRAM_ATTR gpioISR() { ... }

// Data accessed from IRAM_ATTR code: must be in DRAM, never a flash const
static DRAM_ATTR uint32_t isrEventFlags = 0;
```

**Rules**:

- All ISR handlers: `IRAM_ATTR`
- Data read by `IRAM_ATTR` code: `DRAM_ATTR` (a flash-resident `static const` will fault)
- Normal task code does **not** need `IRAM_ATTR`

#### ISR vs Task Shared State

`xSemaphoreTake()` (mutex) **cannot** be called from ISR context — it will crash. Use the correct primitive for each communication direction:

| Direction                       | Correct primitive                                  |
| ------------------------------- | -------------------------------------------------- |
| ISR → task (data)               | `xQueueSendFromISR()` + `portYIELD_FROM_ISR()`     |
| ISR → task (signal)             | `xSemaphoreGiveFromISR()` + `portYIELD_FROM_ISR()` |
| Task → task                     | `xSemaphoreTake()` / mutex                         |
| Simple flag (single writer ISR) | `volatile bool` + `portENTER_CRITICAL_ISR()`       |

#### RISC-V Alignment

ESP32-C3 faults on unaligned multi-byte loads. Never cast a `uint8_t*` buffer to a wider pointer type and dereference it directly. Use `memcpy` for any unaligned read:

```cpp
// WRONG — faults if buf is not 4-byte aligned:
uint32_t val = *reinterpret_cast<const uint32_t*>(buf);

// CORRECT:
uint32_t val;
memcpy(&val, buf, sizeof(val));
```

This applies to all cache deserialization code and any raw buffer-to-struct casting. `__attribute__((packed))` structs have the same hazard when accessed via member reference.

#### Template and `std::function` Bloat

Each template instantiation generates a separate binary copy. `std::function<void()>` adds ~2–4 KB per unique signature and heap-allocates its closure. Avoid both in library code and any path called from the render loop:

```cpp
// Avoid — heap-allocating, large binary footprint:
std::function<void()> callback;

// Prefer — zero overhead:
void (*callback)() = nullptr;

// For member function + context (common activity callback pattern):
struct Callback { void* ctx; void (*fn)(void*); };
```

When a template is necessary, limit instantiations: use explicit template instantiation in a `.cpp` file to prevent the compiler from generating duplicates across translation units.

---

### Error Handling Philosophy

**Pattern Hierarchy**:

1. **LOG_ERR + return false** (90%): `LOG_ERR("MOD", "Failed: %s", reason); return false;`
2. **LOG_ERR + fallback**: `LOG_ERR("MOD", "Unavailable"); useDefault();`
3. **assert(false)**: Only for fatal "impossible" states. There are two in-tree uses: a missing framebuffer in `GfxRenderer::begin()` ([lib/GfxRenderer/GfxRenderer.cpp:133](lib/GfxRenderer/GfxRenderer.cpp)) and the `ActivityManager` destructor, which must never run ([src/activities/ActivityManager.h:75](src/activities/ActivityManager.h))
4. **ESP.restart()**: Reserved for deliberate reboots, not error handling. In-tree uses are firmware update completion (`OtaUpdateActivity`, `SdFirmwareUpdateActivity`), the heap-defrag silent restarts and the USB-storage handoff in `src/main.cpp`, and one backstop after a failed framebuffer restore ([lib/GfxRenderer/GfxRenderer.cpp:172-180](lib/GfxRenderer/GfxRenderer.cpp))

**Rules**: NO exceptions, NO abort(), ALWAYS log before error return

### Heap Buffer Allocation

**Prefer `makeUniqueNoThrow` over `malloc`.** Both are nothrow (return `nullptr` on OOM rather than calling `abort()`), but `malloc` requires a manual `free` on every return path — a common source of leaks. `makeUniqueNoThrow<uint8_t[]>(size)` from `lib/Memory/Memory.h` frees automatically when it goes out of scope.

**Preferred pattern**:

```cpp
#include <Memory.h>

auto buffer = makeUniqueNoThrow<uint8_t[]>(bufferSize);
if (!buffer) {
  LOG_ERR("MODULE", "OOM: %d bytes", bufferSize);
  return false;
}

processData(buffer.get(), bufferSize);
// freed automatically — no manual free needed, no leak on early return
```

**`malloc` or `new (std::nothrow)` are still acceptable** when the buffer must be passed to a C API that takes ownership and frees it itself (e.g., certain SDK callbacks). In that case follow the manual pattern:

```cpp
auto* buffer = static_cast<uint8_t*>(malloc(bufferSize));  // or new (std::nothrow) uint8_t[bufferSize]
if (!buffer) {
  LOG_ERR("MODULE", "OOM: %d bytes", bufferSize);
  return false;
}
sdkApiThatTakesOwnership(buffer, bufferSize);  // SDK calls free() / delete[]
```

**Rules**:

- **Prefer `makeUniqueNoThrow`** — automatic cleanup eliminates leak risk on error paths
- **ALWAYS check for nullptr** after any allocation and `LOG_ERR` before returning false
- **Raw allocation only** when a C API takes ownership; document why in a comment

**Examples in codebase**:

- Memory utilities: [Memory.h](lib/Memory/Memory.h) (`makeUniqueNoThrow`)
- Cover image buffers: `coverBuffer` in [HomeActivity.cpp](src/activities/home/HomeActivity.cpp)
- Bitmap rendering: the scaling row buffers in `GfxRenderer::drawBitmap()` / `drawBitmap1Bit()`, and the BW snapshot chunks in `GfxRenderer::storeBwBuffer()` ([GfxRenderer.cpp](lib/GfxRenderer/GfxRenderer.cpp))

### Heap Allocation with `new`: Always Use `makeUniqueNoThrow`

**CRITICAL**: With `-fno-exceptions`, bare `new` on OOM calls `abort()` — it does NOT return `nullptr`. Always use `makeUniqueNoThrow` from `lib/Memory/Memory.h`, which wraps `new (std::nothrow)` and returns a `std::unique_ptr` that is null on OOM and automatically frees on scope exit.

**Preferred pattern**:

```cpp
#include <Memory.h>

auto obj = makeUniqueNoThrow<MyClass>(args);
if (!obj) { LOG_ERR("MOD", "OOM: MyClass"); return false; }

auto buf = makeUniqueNoThrow<uint8_t[]>(size);
if (!buf) { LOG_ERR("MOD", "OOM: %d bytes", size); return false; }

// Pass to C APIs via .get(); unique_ptr frees automatically on return
someApi(buf.get(), size);
```

**`new (std::nothrow)` directly is acceptable** when the object must be passed to a C API that takes ownership and calls `delete` itself:

```cpp
auto* obj = new (std::nothrow) MyClass(args);
if (!obj) { LOG_ERR("MOD", "OOM: MyClass"); return false; }
sdkApiThatTakesOwnership(obj);  // SDK calls delete
```

**Rules**:

- **Prefer `makeUniqueNoThrow`** — automatic cleanup eliminates leak risk on error paths
- **NEVER use bare `new`** — always `makeUniqueNoThrow` or `new (std::nothrow)`
- **ALWAYS `LOG_ERR` before returning false** on OOM
- **Use `.get()`** to pass the raw pointer to C-style APIs; ownership stays with the `unique_ptr`
- **`new (std::nothrow)` directly only** when a C API takes ownership; document why in a comment

**Examples in codebase**:

- Memory utilities: [Memory.h](lib/Memory/Memory.h) (`makeUniqueNoThrow`)

---

## UI and Orientation Guidelines

### Orientation-Aware Logic

* No Hardcoding: Never assume 800 or 480. Use renderer.getScreenWidth() and renderer.getScreenHeight().
* Viewable Area: Use renderer.getOrientedViewableTRBL() to stay within physical bezel margins.

### Logical Button Mapping

**Source**: `MappedInputManager::mapButton()`, [src/MappedInputManager.cpp:62-123](src/MappedInputManager.cpp)

Constraint: Physical button positions are fixed on hardware, but their logical functions change based on user settings and screen orientation.

**Button Categories**:

1. **Physical Fixed** (Up/Down side buttons):
   
   - `Button::Up` → Always `HalGPIO::BTN_UP`
   
   - `Button::Down` → Always `HalGPIO::BTN_DOWN`

2. **User Remappable** (Front buttons):
   
   - `Button::Back` → Maps to `SETTINGS.frontButtonBack` (hardware index)
   
   - `Button::Confirm` → Maps to `SETTINGS.frontButtonConfirm`
   
   - `Button::Left` → Maps to `SETTINGS.frontButtonLeft`
   
   - `Button::Right` → Maps to `SETTINGS.frontButtonRight`

3. **Reader-Specific** (Page navigation with optional swap):
   
   - `Button::PageBack` → Uses side button (swappable via `SETTINGS.sideButtonLayout`)
   
   - `Button::PageForward` → Uses side button (swappable)
   
   - `SETTINGS.sideButtonLayout == SIDE_BUTTONS_DISABLED` makes both return `false`

4. **Derived** (no hardware button of their own):
   
   - `Button::Power` → Always `HalGPIO::BTN_POWER`, bypasses remapping
   
   - `Button::NavNext` / `NavPrevious` → side Down/Up **or** front Right/Left, axis-flipped by `isNavDirectionSwapped()`
   
   - `Button::ScreenLeft` / `ScreenRight` / `ScreenUp` / `ScreenDown` → screen-space directions remapped through `mapScreenDirection()` per `renderer.getOrientation()`, honouring `SETTINGS.frontButtonFollowOrientation`

**Implementation**:

- Activities use **logical buttons** (e.g., `Button::Confirm`)
- `MappedInputManager` translates to **physical hardware buttons**
- User can remap front buttons in settings
- Orientation changes handled separately by renderer coordinate transforms

**Rule**: Always use `MappedInputManager::Button::*` enums, never raw `HalGPIO::BTN_*` indices (except in ButtonRemapActivity).

### UITheme (The GUI Macro)

* Rule: All UI rendering must go through the GUI macro (UITheme). 
* Do not hardcode fonts, colors, or positioning. This ensures orientation-aware layout consistency.

---

## Common Patterns

### Singleton Access

**Available Singletons**:

```cpp
#define SETTINGS CrossPointSettings::getInstance()      // User settings
#define APP_STATE CrossPointState::getInstance()        // Runtime state
#define GUI UITheme::getInstance().getTheme()           // Current theme (the Theme, not the UITheme)
#define Storage HalStorage::getInstance()               // SD card I/O
#define I18N I18n::getInstance()                        // Internationalization
```

`activityManager` is a plain `extern` global rather than a macro ([src/activities/ActivityManager.h:120](src/activities/ActivityManager.h)); so are the HAL singletons listed in the HAL table above.

### Activity Lifecycle and Memory Management

**Source**: [src/activities/ActivityManager.h](src/activities/ActivityManager.h), [src/activities/ActivityManager.cpp](src/activities/ActivityManager.cpp)

**CRITICAL**: Activities are **heap-allocated** and **destroyed on exit**. Ownership lives in the `activityManager` singleton, not in `main.cpp`:

```cpp
// ActivityManager owns exactly one current activity plus a stack of suspended ones
std::vector<std::unique_ptr<Activity>> stackActivities;
std::unique_ptr<Activity> currentActivity;

void ActivityManager::exitActivity(const RenderLock& lock) {
  if (currentActivity) {
    currentActivity->onExit();
    currentActivity.reset();  // Activity destroyed here
  }
}
```

**Navigation API** — activities never touch `currentActivity` themselves:

- `activityManager.replaceActivity(...)` and the `goTo…()` / `goHome()` wrappers destroy the current activity and clear the stack
- `pushActivity()` moves the current activity onto `stackActivities` instead of destroying it. It does **not** get `onExit()` — it keeps every buffer it allocated. `popActivity()` runs `onExit()` on the top one, destroys it, and restores the one beneath (or goes home when the stack is empty)
- From inside an activity, use `startActivityForResult()` / `setResult()` / `finish()` ([src/activities/Activity.h:57-64](src/activities/Activity.h))
- Navigation requested from `loop()` is **deferred**: it sets `pendingAction` and is applied after `loop()` returns, so an activity never deletes itself mid-call

**Memory Implications**:

- Activity navigation = destroy old activity + construct the next one
- Any memory allocated in `onEnter()` MUST be freed in `onExit()`
- A pushed activity is *not* destroyed and does not run `onExit()` — budget for its heap staying allocated underneath the sub-activity
- Member `HalFile` handles MUST be closed in `onExit()` (local `HalFile` variables auto-close via destructor)

**Activity Pattern**:

```cpp
void onEnter()        { Activity::onEnter(); /* alloc buffers */ requestUpdate(); }
void loop()           { /* read input, mutate state, requestUpdate() */ }
void render(RenderLock&&) { /* draw; runs on the shared render task */ }
void onExit()         { Activity::onExit(); /* free buffers, close member HalFiles */ }
```

`mappedInputManager.update()` is called once per iteration by the Arduino `loop()` in [src/main.cpp:593](src/main.cpp), so activities read button state without polling it themselves.

**Critical**: Free resources in reverse order in `onExit()`.

### FreeRTOS Task Guidelines

**Source**: [src/activities/ActivityManager.cpp:32-46](src/activities/ActivityManager.cpp)

**There is exactly one application-created FreeRTOS task.** `ActivityManager::begin()` spawns the shared render task and nothing else in `src/` or `lib/` calls `xTaskCreate*`:

```cpp
xTaskCreatePinnedToCore(&renderTaskTrampoline, "ActivityManagerRender",
                        8192,               // Stack size, in BYTES (ESP-IDF, not words)
                        this,               // Parameters
                        1,                  // Priority
                        &renderTaskHandle,
                        renderTaskCore);    // Core 1 when configNUM_CORES > 1, else core 0
```

It blocks on `ulTaskNotifyTake()` and renders the current activity under a `RenderLock` whenever `requestUpdate()` fires. Activities therefore do **not** own render tasks: they implement `render(RenderLock&&)` and let the manager schedule it.

**Rules**:

- Do not add a per-activity task. Push work into `render()` or the activity's `loop()` instead
- `requestUpdate(immediate)` defers or forces a render; `requestUpdateAndWait()` blocks until it completes and must not be called from the render task or while holding a `RenderLock`
- If a task is ever genuinely needed, `vTaskDelete()` it in `onExit()` before the activity is destroyed, and guard shared state with a mutex
- Monitor with `uxTaskGetStackHighWaterMark()` if you see crashes

### Global Font Loading

**Source**: [src/main.cpp:62-125](src/main.cpp)

**All built-in fonts are loaded as global objects** at firmware startup:

- Noto Serif: 12, 14, 16, 18pt (4 styles each: regular, bold, italic, bold-italic)
- Noto Sans: 12, 14, 16, 18pt (4 styles each)
- Noto Sans 8pt regular, as the single-style `smallFontFamily`
- Ubuntu UI fonts: 10, 12pt (2 styles: regular, bold)

**Total** (with `OMIT_FONTS` unset): 37 `EpdFont` + 11 `EpdFontFamily` globals

**Compilation Flag**:

```cpp
#ifndef OMIT_FONTS
  // Everything except notoserif14, smallFont and the two UI families
#endif
```

**Implications**:

- Glyph data lives in **Flash** (`static const` arrays in `lib/EpdFont/builtinFonts/`)
- The reader families (Noto Serif / Noto Sans) are 2-bit and DEFLATE-compressed in groups; `FontDecompressor` expands a group into **DRAM** on demand and caches it ([lib/EpdFont/FontDecompressor.h:66-71](lib/EpdFont/FontDecompressor.h)). The Ubuntu UI fonts and `notosans_8_regular` are converted without `--2bit --compress`, so they carry `groups == nullptr` and are read straight from Flash
- `OMIT_FONTS` can reduce binary size for minimal builds
- Font IDs are in [src/fontIds.h](src/fontIds.h), which is **generated** by `lib/EpdFont/scripts/build-font-ids.sh` — the IDs are hashes, so never hand-edit them

**Usage**:

```cpp
#include "fontIds.h"

renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
renderer.drawText(UI_12_FONT_ID, x, y, "Hello", true);
```

---

## Testing and Debugging

### Build Commands

**Via CLI**:

```bash
# Build firmware (default environment)
pio run

# Build and upload to device
pio run -t upload

# Build specific environment (C3 default, or an S3 board)
pio run -e gh_release
pio run -e sticky
pio run -e x4pro -t upload

# Clean build artifacts
pio run -t clean
```

**A change is only proven on the family you built.** The C3 and each S3 board are separate binaries; `pio run` alone builds `default` (C3) only.

**Via VS Code**:

* Use PlatformIO toolbar: Build (✓), Upload (→), Clean (🗑️)
* Or Command Palette: `PlatformIO: Build`, `PlatformIO: Upload`, etc.

### Monitoring and Debugging

```bash
# Enhanced monitor with color/logging (recommended)
python3 scripts/debugging_monitor.py

# Standard PlatformIO monitor
pio device monitor
```

**Via VS Code**: Click Monitor (🔌) button in PlatformIO toolbar

### Code Quality

```bash
# Static analysis (cppcheck)
pio check

# Format only Git-modified C/C++ files, on every host
./bin/clang-format-fix -g
```

Do not run raw `clang-format` or probe it with `command -v`; use the wrapper even for diagnostics.

### Debugging Crashes

**Common Crash Causes**:

1. **Out of Memory** (Most common):
   
   ```cpp
   LOG_DBG("MEM", "Free heap: %d bytes", ESP.getFreeHeap());
   ```
   
   - Monitor heap usage throughout activity lifecycle
   
   - Check if large allocations (>10KB) occur before crash
   
   - Verify buffers are freed in `onExit()`

2. **Stack Overflow**:
   
   ```cpp
   LOG_DBG("TASK", "Stack high water: %d", uxTaskGetStackHighWaterMark(taskHandle));
   ```
   
   - Occurs during deep recursion or large local variables
   
   - Increase task stack size in `xTaskCreate()` (2048 → 4096)
   
   - Move large buffers to heap with malloc

3. **Use-After-Free**:
   
   - Activity deleted but task still running
   
   - Always `vTaskDelete()` in `onExit()` BEFORE activity destruction
   
   - Set pointers to `nullptr` after `free()`

4. **Corrupt Cache Files**:
   
   - Delete `.crosspoint/` directory on SD card
   
   - Forces clean re-parse of all EPUBs
   
   - Check file format versions in [docs/file-formats.md](docs/file-formats.md)

5. **Watchdog Timeout**:
   
   - Loop/task blocked for >5 seconds
   
   - Add `vTaskDelay(1)` in tight loops
   
   - Check for blocking I/O operations

**Verification Steps**:

1. Check serial output for stack traces
2. Monitor heap with `ESP.getFreeHeap()` before/after operations
3. Verify task deletion with task list (`vTaskList()`)
4. Test with `LOG_LEVEL=2` (debug logging enabled)

---

## Git Workflow and Repository Awareness

### Repository Detection Protocol

**CRITICAL**: ALWAYS verify repository context before git operations. This could be:

- A **fork** with `origin` pointing to personal repo, `upstream` to main repo
- A **direct clone** with `origin` pointing to main repo
- Multiple collaborator remotes

**Verification Commands** (run at session start):

```bash
# Check current branch
git branch --show-current

# Check all remotes
git remote -v

# Check working tree status
git status --short
```

**Example Output** (forked repository):

```text
origin      https://github.com/<your-username>/crosspoint-reader.git (fetch/push)
upstream    https://github.com/crosspoint-reader/crosspoint-reader.git (fetch/push)
```

### Git Operation Rules

1. Integration branches and PR comparisons target `develop`, not `master` or the remote's symbolic HEAD.
2. Never push to any remote or open/close a PR without explicit user approval. Complete local work and any requested local commit, then stop.
3. If the user explicitly approves a push, inspect remotes again and use `fork` for the feature branch unless the user specifies otherwise.
4. Never add Claude, Codex, or assistant self-attribution as a commit co-author or generated-by trailer.
5. When a change supersedes or adapts another person's PR, verify the original human author from Git/GitHub and add that person as `Co-Authored-By`; skip bot authors.

### Branch Naming Convention

**For feature/fix branches**:

```text
feature/<short-description>       # New features
fix/<issue-number>-<description>  # Bug fixes
refactor/<component-name>         # Code refactoring
docs/<topic>                      # Documentation updates
```

**Examples**:

- `feature/sd-download-progress`
- `fix/123-orientation-crash`
- `refactor/hal-storage`

### Commit Message Format

**Pattern**:

```text
<type>: <short summary (50 chars max)>

<optional detailed description>
```

**Types**: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `perf`

**Example**:

```text
feat: add real-time SD download progress bar

Implements progress tracking for book downloads using
UITheme progress bar component with heap-safe updates.

Tested in all 4 orientations with 5MB+ files.
```

### When to Commit

**DO commit when**:

- User explicitly requests: "commit these changes"
- Feature is complete and tested on device
- Bug fix is verified working
- Refactoring preserves all functionality
- All tests pass (`pio run` succeeds)

**DO NOT commit when**:

- Changes are untested on actual hardware
- Build fails or has warnings
- Experimenting or debugging in progress
- User hasn't explicitly requested commit
- Files excluded by `.gitignore` would be included — always run `git status` and cross-check against `.gitignore` before staging (e.g., `*.generated.h`, `.pio/`, `compile_commands.json`, `platformio.local.ini`)

**Rule**: **If uncertain, ASK before committing.**

---

## Generated Files and Build Artifacts

### Files Generated by Build Scripts

**NEVER manually edit these files** - they are regenerated automatically:

1. **HTML/JS Headers** (generated by `scripts/build_html.py`):
   
   - `src/network/html/*.generated.h` and `src/network/html/js/*.generated.h`
   
   - **Source**: the `.html` and `.js` files that sit *next to* the generated headers — `src/network/html/{HomePage,FilesPage,FontsPage,SettingsPage}.html` and `src/network/html/js/jszip.min.js`. There is no `data/` directory; the script walks `src/` and writes each header beside its source.
   
   - **Triggered**: During PlatformIO `pre:` build step
   
   - **What it emits**: the source is minified (HTML only), gzipped at level 9, and written as a `constexpr char[] PROGMEM` byte array plus `…CompressedSize` and `…OriginalSize` constants
   
   - **To modify**: Edit the source `.html` / `.js`, not the generated headers

2. **I18n Headers** (generated by `scripts/gen_i18n.py`):
   
   - `lib/I18n/I18nKeys.h`, `lib/I18n/I18nStrings.h`, `lib/I18n/I18nStrings.cpp`
   
   - **Source**: YAML translation files in `lib/I18n/translations/` (one per language)
   
   - **To modify**: Edit source YAML files, then run `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/`
   
   - **Commit**: Source YAML files only. All three generated files (`I18nKeys.h`, `I18nStrings.h`, `I18nStrings.cpp`) are in `.gitignore` and regenerated at build time.

3. **Build Artifacts** (in `.gitignore`):
   
   - `.pio/` - PlatformIO build output
   
   - `build/` - Compiled binaries
   
   - `*.generated.h` - Any auto-generated headers
   
   - `compile_commands.json` - LSP/IDE metadata

### Modifying Generated Content Workflow

**To change HTML pages**:

1. Edit source: `src/network/html/<PageName>.html`
2. Build: `pio run` (auto-triggers `scripts/build_html.py`)
3. Generated headers update in place: `src/network/html/<PageName>Html.generated.h`
4. **Commit ONLY** source HTML, NOT generated `.generated.h` files

**To add/modify translations (i18n)**:

1. Edit or add YAML file: `lib/I18n/translations/<language>.yaml`
   - Each file must contain: `_language_name`, `_language_code`, `_order`, `_bcp47`, and `STR_*` keys
   - English (`english.yaml`) is the reference; missing keys in other languages fall back to English
2. Run generator: `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/`
3. Generated files update: `I18nKeys.h`, `I18nStrings.h`, `I18nStrings.cpp`
4. **Commit** source YAML files only. All three generated files are in `.gitignore` and regenerated at build time.

**To use translated strings in code**:

```cpp
#include <I18n.h>
// Use tr() macro with StrId enum (defined in generated I18nKeys.h)
renderer.drawText(UI_12_FONT_ID, x, y, tr(STR_LOADING), true);
```

**To change the built-in fonts**:

1. Place the source TTFs under `lib/EpdFont/builtinFonts/source/<Family>/`
2. Run `lib/EpdFont/scripts/convert-builtin-fonts.sh`, which calls `fontconvert.py` per face and rewrites `lib/EpdFont/builtinFonts/<name>.h`. Reader faces are generated with `--2bit --compress --pnum --zopfli`; UI faces are not
3. Regenerate the IDs: `lib/EpdFont/scripts/build-font-ids.sh` prints the whole of `src/fontIds.h` to stdout. The IDs are SHA-256 digests of the generated headers, so they change whenever a face is reconverted
4. Update the global font objects in [src/main.cpp:62-125](src/main.cpp) and the `insertFont()` calls that follow

---

## Local Development Configuration

### platformio.local.ini (Personal Overrides)

**Purpose**: Personal development settings that should NEVER be committed.

**Use Cases**:

- Serial port configuration (varies by machine)
- Debug flags for specific testing
- Local build optimizations
- Developer-specific paths

**Example** `platformio.local.ini`:

```ini
# platformio.local.ini (gitignored)
[env:default]
upload_port = COM7              # Windows: COMx, Linux: /dev/ttyUSBx
monitor_port = COM7

build_flags =
  ${base.build_flags}
  -DMY_DEBUG_FLAG=1             # Personal debug flags
  -DTEST_FEATURE_ENABLED=1
```

**Configuration Hierarchy**:

1. `platformio.ini` - **Committed**, shared project settings
2. `platformio.local.ini` - **Gitignored**, personal overrides
3. Local file extends/overrides base config

**Rules**:

- **NEVER commit** `platformio.local.ini`
- **NEVER put** personal info (serial ports, credentials) in main `platformio.ini`
- Use `${base.build_flags}` to extend (not replace) base flags

---

## Testing and Verification Workflow

### Testing Checklist

**AI agent scope** (what you CAN verify):

1. ✅ **Build**: Build once after the last code edit with the relevant `pio run` target. Do not clean by default, repeat a target that already passed, or rebuild after formatting/comment-only/documentation-only changes.
2. ✅ **Quality**: `pio check` when relevant + `./bin/clang-format-fix -g`
3. ✅ **Format**: Commit messages (`feat:`/`fix:`), no `.gitignore`-excluded files staged (e.g., `*.generated.h`, `.pio/`, `platformio.local.ini`)
4. ✅ **CI**: Fix GitHub Actions failures before review
5. ✅ **Code review**: Ensure orientation-aware logic is correct in all 4 modes by inspecting switch/case coverage

**Human tester scope** (flag these for the user):
6. 🔲 **Device**: Test on hardware
7. 🔲 **Orientations**: Verify all 4 modes (Portrait/Inverted/Landscape CW/CCW)
8. 🔲 **Heap**: `ESP.getFreeHeap()` > 50KB, no leaks
9. 🔲 **Cache**: If EPUB modified, delete `.crosspoint/` and verify re-parse

### CI/CD Pipeline Awareness

**GitHub Actions** run automatically on pull requests:

| Workflow      | File                                        | Purpose                                                      |
| ------------- | ------------------------------------------- | ------------------------------------------------------------ |
| CI (build)    | `.github/workflows/ci.yml`                  | All quality gates: clang-format, cppcheck, firmware builds, unit tests |
| PR Formatting | `.github/workflows/pr-formatting-check.yml` | **PR title only** — semantic-commit format check              |
| Release Build | `.github/workflows/release.yml`             | Production releases                                           |
| RC Build      | `.github/workflows/release_candidate.yml`   | Release candidates                                            |
| Fonts Release | `.github/workflows/release-fonts.yml`       | Manual (`workflow_dispatch`) SD-card font build and publish   |

**`ci.yml` jobs** (run on PRs and on pushes to `master`):

| Job            | What it does                                                                                        |
| -------------- | --------------------------------------------------------------------------------------------------- |
| `clang-format` | Runs `./bin/clang-format-fix` with clang-format-21 and fails if `git diff` is non-empty               |
| `cppcheck`     | `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`                        |
| `build`        | Matrix build of **five** environments: `default`, `sticky`, `x4pro`, `x4c`, `papermono` — each uploaded as `firmware.bin` / `firmware-<board>.bin` |
| `unit-tests`   | CMake/Ninja + ctest over `test/`, twice: `plain` and `asan` (`-DCROSSPOINT_SANITIZE=ON`, `ASAN_OPTIONS=detect_leaks=1`) |
| `test-status`  | Aggregating gate used as the PR required check; fails if any of the above failed or was cancelled     |

**Rules**:

- **Fix CI failures BEFORE** requesting review
- `ci.yml` runs on: pull requests, and pushes to `master`
- `clang-format` job fails → Run `./bin/clang-format-fix -g`
- `build` job fails → Fix compile errors; note a change can build on C3 and still fail on an S3 board (or vice versa)
- `unit-tests` job fails → Reproduce locally with `./bin/run-tests` (add `--asan` for the sanitizer variant)
- `PR Formatting` fails → Your **PR title** is not in semantic-commit form (`feat:`, `fix:`, `chore:`, `docs:`, …); it says nothing about code formatting

---

## Serial Monitoring and Live Debugging

### Serial Monitor Options

1. **Enhanced**: `python3 scripts/debugging_monitor.py` (color-coded, recommended)
2. **Standard**: `pio device monitor` (basic, no colors)
3. **VS Code**: Monitor (🔌) button (IDE-integrated)

### Live Debugging Patterns

**Heap**: `LOG_DBG("MEM", "Free: %d", ESP.getFreeHeap());` (every 5s in loop)
**Stack**: `uxTaskGetStackHighWaterMark(nullptr)` (< 512 bytes → increase stack)
**Flush**: `logSerial.flush();` (force output before crash)

**Port Detection**: Windows: `mode` | Linux: `ls /dev/ttyUSB* /dev/ttyACM*` or `dmesg | grep tty`

---

## Cache Management and Invalidation

### Cache Structure on SD Card

**Location**: `.crosspoint/` directory on SD card root

**Structure**: `.crosspoint/epub_<hash>/{book.bin, progress.bin, cover.bmp, sections/<n>.bin}`

**Per-format prefixes**: `epub_`, `fb2_`, `txt_`, `xtc_` ([src/util/BookCacheUtils.cpp:18-21](src/util/BookCacheUtils.cpp))

**Hash**: `std::hash<std::string>{}(filepath)` ([lib/Epub/Epub.h:43](lib/Epub/Epub.h)) → Moving/renaming file = new hash = lost progress

### Cache Invalidation Rules

**Cache is automatically invalidated when**:

1. **File format version changes** (see `docs/file-formats.md`)
   
   - `book.bin` version number incremented (`BOOK_CACHE_VERSION`)
   
   - `sections/<n>.bin` version number incremented (`SECTION_FILE_VERSION`)
2. **Render settings change**: section files are keyed on the whole `ReaderRenderSpec`, and *any* differing field discards and rebuilds the file ([lib/Epub/Epub/ReaderRenderSpec.h](lib/Epub/Epub/ReaderRenderSpec.h)). The spec is built by `CrossPointSettings::readerRenderSpec(width, height)` ([src/CrossPointSettings.cpp:263-277](src/CrossPointSettings.cpp)) from:
   
   - `fontId` — derived from `SETTINGS.fontFamily` + `SETTINGS.fontPointSize`, or from `SETTINGS.sdFontFamilyName` when an SD card font is selected
   
   - `lineCompression` — derived from `SETTINGS.lineSpacing` (and the active family)
   
   - `extraParagraphSpacing`, `paragraphAlignment`, `hyphenationEnabled`, `embeddedStyle`, `imageRendering`, `focusReadingEnabled`
3. **Viewport dimensions change** (`viewportWidth` / `viewportHeight`, passed in by the reader):
   
   - Screen orientation change
   
   - Screen margins (`SETTINGS.screenMargin`), which shrink the viewport
   
   - Display resolution change
4. **Book file modified**:
   
   - Moved, renamed, or content changed (new hash)

**Manual Cache Clear** (safe operations):

```bash
# Delete ALL caches (forces full regeneration)
rm -rf /path/to/sd/.crosspoint/

# Delete specific book cache
rm -rf /path/to/sd/.crosspoint/epub_<hash>/

# Keep progress, delete only rendered sections
rm -rf /path/to/sd/.crosspoint/epub_<hash>/sections/
```

**When to Clear Cache**:

- EPUB parsing errors after code changes to `lib/Epub/`
- Corrupt rendering (missing text, wrong layout)
- Testing cache generation logic
- After modifying:
  - `lib/Epub/Epub/Section.cpp`
  - `lib/Epub/Epub/BookMetadataCache.cpp`
  - Render settings in `CrossPointSettings`

### Cache File Format Versioning

**Source**: the constant in each format's own file — the version is the code, not the doc.

| Cache file                        | Constant                       | Version | Defined in                                                    |
| --------------------------------- | ------------------------------ | ------- | ------------------------------------------------------------- |
| `book.bin` (EPUB metadata)        | `BOOK_CACHE_VERSION`           | **10**  | [lib/Epub/Epub/BookMetadataCache.cpp:14](lib/Epub/Epub/BookMetadataCache.cpp) |
| `sections/<n>.bin` (EPUB layout)  | `SECTION_FILE_VERSION`         | **45**  | [lib/Epub/Epub/Section.cpp:50](lib/Epub/Epub/Section.cpp)      |
| CSS cache                         | `CssParser::CSS_CACHE_VERSION` | **12**  | [lib/Epub/Epub/css/CssParser.h:53](lib/Epub/Epub/css/CssParser.h) |
| FB2 metadata                      | `FB2_CACHE_VERSION`            | **2**   | [lib/Fb2/Fb2.cpp:13](lib/Fb2/Fb2.cpp)                          |
| FB2 section layout                | `FB2_SECTION_FILE_VERSION`     | **4**   | [lib/Fb2/Fb2/Fb2Section.cpp:20](lib/Fb2/Fb2/Fb2Section.cpp)     |
| TXT page index                    | `CACHE_VERSION`                | **3**   | [lib/Txt/TxtPageIndex.h:15](lib/Txt/TxtPageIndex.h)            |

Section files are named `sections/<spineIndex>.bin`, not `section.bin` ([lib/Epub/Epub/Section.cpp:80](lib/Epub/Epub/Section.cpp)).

**Version Increment Rules**:

1. **ALWAYS increment version** BEFORE changing binary structure
2. Version mismatch → Cache auto-invalidated and regenerated
3. Document format changes in `docs/file-formats.md`
4. `SECTION_FILE_VERSION` has two companion sentinels that must stay consistent with it:
   - `SECTION_FILE_INCOMPLETE_VERSION` (0) is written first and replaced by the real version as the last step, so an interrupted build is never mistaken for a valid cache
   - `SECTION_FILE_PARTIAL_VERSION` is *derived* from `SECTION_FILE_VERSION` (`0xFE - (SECTION_FILE_VERSION - 28)`) so it changes in lockstep — do not hardcode it

**Example** (incrementing section format version):

```cpp
// lib/Epub/Epub/Section.cpp
constexpr uint8_t SECTION_FILE_VERSION = 46;  // Was 45, now 46

// Add new field to structure
struct PageLine {
  // ... existing fields ...
  uint16_t newField;  // New field added
};
```

---

Philosophy: We are building a dedicated e-reader, not a Swiss Army knife. If a feature adds RAM pressure without significantly improving the reading experience, it is Out of Scope.

<!-- SPECKIT START -->
For additional context about technologies to be used, project structure,
shell commands, and other important information, read the current plan
<!-- SPECKIT END -->
