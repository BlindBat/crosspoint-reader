"""
PlatformIO post-load script: register a `unit-tests` custom target so
`pio run -t unit-tests` builds and runs the host gtest suites under test/.

The target shells out to CMake/CTest; the gtest framework is fetched and
the suites are built outside the PlatformIO/ESP-IDF toolchain (this is a
host build, not a firmware build).

cmake/ctest/ninja are usually NOT on PATH for PlatformIO users, so the
tools are resolved from the PlatformIO-bundled packages (tool-cmake,
tool-ninja) first, falling back to ~/.platformio/packages and finally to
the bare command name (PATH).
"""

import os
import shutil

Import("env")  # noqa: F821  -- provided by PlatformIO at script load

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
BUILD_DIR = os.path.join(PROJECT_DIR, "build", "test")
TEST_SRC_DIR = os.path.join(PROJECT_DIR, "test")

EXE_SUFFIX = ".exe" if os.name == "nt" else ""


def _package_dir(package):
    """Locate a PlatformIO tool package, tolerating unmanaged installs."""
    pkg_dir = None
    try:
        pkg_dir = env.PioPlatform().get_package_dir(package)  # noqa: F821
    except Exception:
        pkg_dir = None
    if pkg_dir and os.path.isdir(pkg_dir):
        return pkg_dir
    fallback = os.path.expanduser(os.path.join("~", ".platformio", "packages", package))
    return fallback if os.path.isdir(fallback) else None


def _resolve_tool(name, package, *subdirs):
    """Absolute path of a bundled tool, else the bare name if on PATH, else None."""
    pkg_dir = _package_dir(package)
    if pkg_dir:
        candidate = os.path.join(pkg_dir, *subdirs, name + EXE_SUFFIX)
        if os.path.isfile(candidate):
            return candidate
    if shutil.which(name):
        return name
    return None


CMAKE = _resolve_tool("cmake", "tool-cmake", "bin") or "cmake"
CTEST = _resolve_tool("ctest", "tool-cmake", "bin") or "ctest"
NINJA = _resolve_tool("ninja", "tool-ninja")

configure_cmd = (
    f'"{CMAKE}" -S "{TEST_SRC_DIR}" -B "{BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release'
    # PRE_TEST defers gtest test discovery from build time to ctest time;
    # keeps the build usable in sandboxes that block freshly linked binaries.
    " -DCMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE=PRE_TEST"
)
if NINJA:
    configure_cmd += f' -G Ninja -DCMAKE_MAKE_PROGRAM="{NINJA}"'

env.AddCustomTarget(  # noqa: F821
    name="unit-tests",
    dependencies=None,
    actions=[
        configure_cmd,
        f'"{CMAKE}" --build "{BUILD_DIR}"',
        f'"{CTEST}" --test-dir "{BUILD_DIR}" --output-on-failure -j',
    ],
    title="Host unit tests",
    description="Build and run gtest suites in test/ via CMake/CTest",
)
