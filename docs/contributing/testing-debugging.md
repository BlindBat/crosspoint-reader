# Testing and Debugging

CrossPoint runs on real hardware, so debugging usually combines local build checks, host-side unit
tests, and on-device logs.

## Local checks

Make sure `clang-format` 21+ is installed and available in `PATH` before running the formatting step.
If needed, see [Getting Started](./getting-started.md).

```sh
./bin/clang-format-fix
./bin/run-tests
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run
```

## Host unit tests

Logic that does not need the device — parsers, caches, layout maths, path handling, serialization —
is covered by host-side Google Test suites under `test/`. They build with CMake + Ninja against the
same production sources the firmware compiles, plus small stubs for the hardware layer.

The `bin/run-tests` wrapper *(fork-only)* is the usual entry point:

```sh
./bin/run-tests                  # configure, build and run every suite
./bin/run-tests --asan           # same, with AddressSanitizer + UBSan (build/test-asan)
./bin/run-tests --filter css     # only suites whose directory or target matches the regex
./bin/run-tests --quick          # skip the two slowest suites (what the pre-push hook runs)
./bin/run-tests --full           # also run pio check afterwards
./bin/run-tests --clean          # delete the build directory first
```

The script finds `cmake`, `ctest` and `ninja` on `PATH` and otherwise falls back to the copies
PlatformIO bundles, so a plain PlatformIO install is enough. A full run goes through `ctest`, and
falls back to running the suite binaries directly if ctest test discovery fails; a `--filter` or
`--quick` run always executes the selected binaries directly. Two environment variables help when
several checkouts build at once: `CROSSPOINT_TEST_BUILD_DIR` overrides the build directory and
`CROSSPOINT_TEST_JOBS` caps build parallelism.

PlatformIO also exposes the same thing as a custom target, registered by
`scripts/register_unit_tests_target.py`:

```sh
pio run -t unit-tests
```

That target exists upstream too and always configures, builds and runs everything;
`bin/run-tests` is the one to use when you want `--filter`, `--asan`, or `--quick`.

You can drive CMake by hand too:

```sh
cmake -S test -B build/test
cmake --build build/test
ctest --test-dir build/test --output-on-failure -j
```

### Adding a suite

Each suite is one directory under `test/` with its own `CMakeLists.txt` declaring a single
`add_executable(<Name>Test ...)`, and one `crosspoint_suite(<dir>)` line in `test/CMakeLists.txt`.
`bin/run-tests` discovers suites by scanning those files, so a new suite is picked up as soon as
both exist — nothing lists them twice. Google Test is fetched via `FetchContent` at the version
pinned in `test/CMakeLists.txt`. The `crosspoint_suite()` wrapper is *(fork-only)* — upstream lists
suites with plain `add_subdirectory()` — and it honours the `CROSSPOINT_SUITE_DIRS` cache variable,
which is how `--filter` and `--quick` configure only the directories they need.

Parser suites should compile the in-tree expat (`lib/expat`) with the firmware's flags,
`-DXML_GE=0 -DXML_CONTEXT_BYTES=1024`, rather than the host system's expat, so host behavior
matches the device. `test/corpus/` *(fork-only)* holds a committed malformed-input corpus that the
parser suites replay; see `test/corpus/README.md`. Host stand-ins shared across suites live in
`test/support/` *(fork-only)*.

CI runs the test program on every PR; the second, sanitized leg (`-DCROSSPOINT_SANITIZE=ON`) is
*(fork-only)*.

## Flash and monitor

Flash firmware:

```sh
pio run --target upload
```

Other boards use their own environment, for example:

```sh
pio run -e x4pro --target upload
```

Open serial monitor:

```sh
pio device monitor
```

Optional enhanced monitor:

```sh
python3 -m pip install pyserial colorama matplotlib
python3 scripts/debugging_monitor.py
```

## Useful bug report contents

- Firmware version and build environment (which `pio run -e …` you flashed)
- Exact steps to reproduce
- Expected vs actual behavior
- Serial logs from boot through failure
- Whether issue reproduces after clearing `.crosspoint/` cache on SD card

## Common troubleshooting references

- [User Guide troubleshooting section](../../USER_GUIDE.md#7-troubleshooting-issues--escaping-bootloop)
- [Webserver troubleshooting](../troubleshooting.md)
