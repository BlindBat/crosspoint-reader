# Getting Started

This guide helps you build and run CrossPoint locally.

## Prerequisites

- PlatformIO Core (`pio`) or VS Code + PlatformIO IDE
- Python 3.8+
- `clang-format` 21+ in your `PATH` (CI uses clang-format 21)
- USB-C cable
- A supported device for hardware testing: Xteink X4 or X3 (ESP32-C3), or one of the ESP32-S3
  boards — Seeed Sticky, Xteink X4 Pro, Xteink X4 Classic, M5Stack Paper Mono

The host test suites additionally need CMake and Ninja. If they are not on your `PATH`,
`./bin/run-tests` *(fork-only)* falls back to the copies PlatformIO bundles, so a plain PlatformIO
install is usually enough.

`./bin/clang-format-fix` prefers a `clang-format-21` binary, falls back to `clang-format`, and
refuses to run anything older than major version 21 (this repository's `.clang-format` uses the
clang-format 21 key `AlignFunctionDeclarations`). Install clang-format 21 if it stops with either of
these:

- `'clang-format' not found in current environment`
- `Error: clang-format is too old: …` / `This repository's .clang-format requires clang-format 21 or newer.`

Examples:

```sh
# Debian/Ubuntu (try this first)
sudo apt-get update && sudo apt-get install -y clang-format-21

# If the package is unavailable, add LLVM apt repo and retry
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 21
sudo apt-get update
sudo apt-get install -y clang-format-21

# macOS (Homebrew)
brew install clang-format
```

Then verify:

```sh
clang-format-21 --version
```

The reported major version must be 21 or newer.

## Clone and initialize

```sh
git clone --recursive https://github.com/crosspoint-reader/crosspoint-reader
cd crosspoint-reader
```

If you already cloned without submodules:

```sh
git submodule update --init --recursive
```

Enable the repository-managed Git hooks (opt-in, once per clone; `pre-push` is *(fork-only)*):

```sh
./bin/install-hooks
```

This points `core.hooksPath` at `.githooks/`: `pre-commit` runs `./bin/clang-format-fix` over every
modified tracked C/C++ file and re-stages the paths that were already staged; `pre-push` runs the
formatting check plus a quick subset of the host tests. Undo with
`git config --unset core.hooksPath`.

## Build

```sh
pio run
```

That builds the `default` environment for the ESP32-C3 X4/X3. Each other board has its own
environment — `sticky`, `x4pro`, `x4c`, `papermono`:

```sh
pio run -e x4pro
```

See [Architecture Overview](./architecture.md#board-targets) for what each one targets.

## Flash

```sh
pio run --target upload
pio run -e x4pro --target upload     # a specific board
```

## Run the host tests

```sh
./bin/run-tests
```

## First checks before opening a PR

```sh
./bin/clang-format-fix
./bin/run-tests
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run
```

## What to read next

- [Architecture Overview](./architecture.md)
- [Development Workflow](./development-workflow.md)
- [Testing and Debugging](./testing-debugging.md)
