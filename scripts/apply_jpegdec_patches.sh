#!/usr/bin/env bash

# Apply CrossPoint's JPEGDEC patches to a JPEGDEC checkout (host test builds).
#
# Usage: apply_jpegdec_patches.sh <jpegdec-src-dir> <patch-dir>
#
# This is the shell twin of scripts/patch_jpegdec.py (the PlatformIO pre-build
# hook): the host test suite fetches the same pinned JPEGDEC commit via CMake
# FetchContent and runs this as PATCH_COMMAND. Idempotency is decided by git
# itself, exactly like the Python script:
#   * `git apply --check --reverse` succeeds -> already applied, skip
#   * `git apply --check`           succeeds -> apply
#   * neither succeeds                       -> abort (diverged source)

set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <jpegdec-src-dir> <patch-dir>" >&2
  exit 2
fi

SRC_DIR="$1"
PATCH_DIR="$2"

shopt -s nullglob
PATCHES=("${PATCH_DIR}"/*.patch)
shopt -u nullglob

if [[ ${#PATCHES[@]} -eq 0 ]]; then
  echo "ERROR: no .patch files in ${PATCH_DIR}" >&2
  exit 1
fi

cd "${SRC_DIR}"

for patch in "${PATCHES[@]}"; do
  name="$(basename "${patch}")"
  if git apply --check --reverse "${patch}" >/dev/null 2>&1; then
    echo "JPEGDEC patch already applied: ${name}"
    continue
  fi
  if ! git apply --check "${patch}" >/dev/null 2>&1; then
    echo "ERROR: JPEGDEC patch ${name} does not apply cleanly" >&2
    exit 1
  fi
  git apply "${patch}"
  echo "Applied JPEGDEC patch: ${name}"
done
