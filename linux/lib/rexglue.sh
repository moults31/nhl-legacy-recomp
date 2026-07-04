#!/usr/bin/env bash
# Build the ReXGlue SDK from source with NHL Legacy correctness patches.
# Called by linux/lib/compile.sh inside Docker — not a user entry point.
#
# Stock prebuilt linux-amd64 zips lack the title-specific fixes in
# docs/rexglue-vulkan-nhl-legacy.patch (jersey font exp_adjust, signed BC5/DXN,
# readback sizing). Windows release builds use a patched SDK; Linux must too.
set -euo pipefail

LINUX_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "${LINUX_DIR}/.." && pwd)"
# shellcheck source=common.sh
source "${LINUX_DIR}/lib/common.sh"

REXGLUE_REF="${REXGLUE_REF:-v0.8.0}"
DEPS="${OUT_LINUX}/deps"
REXGLUE_SRC="${DEPS}/rexglue-sdk"
REXGLUE_INSTALL="${OUT_LINUX}/deps/rexglue-sdk-install/linux-amd64"
PATCH="${ROOT}/docs/rexglue-vulkan-nhl-legacy.patch"
STAMP="${REXGLUE_INSTALL}/.nhl-legacy-patched"

checksum_patch() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "${PATCH}" | awk '{print $1}'
  else
    stat -c '%s %Y' "${PATCH}" 2>/dev/null || stat -f '%z %m' "${PATCH}"
  fi
}

STAMP_CONTENT="${REXGLUE_REF} $(checksum_patch)"

progress_step "ReXGlue SDK setup (ref=${REXGLUE_REF}, NHL-patched)"

mkdir -p "${DEPS}"

if [[ -x "${REXGLUE_INSTALL}/bin/rexglue" && -f "${STAMP}" ]] \
   && [[ "$(cat "${STAMP}")" == "${STAMP_CONTENT}" ]]; then
  progress_log "patched ReXGlue SDK already installed at ${REXGLUE_INSTALL}"
  echo "REXSDK=${REXGLUE_INSTALL}"
  exit 0
fi

if [[ -x "${REXGLUE_INSTALL}/bin/rexglue" && ! -f "${STAMP}" ]]; then
  progress_log "removing unpatched/prebuilt SDK at ${REXGLUE_INSTALL}"
  rm -rf "${DEPS}/rexglue-sdk-install"
fi

if [[ ! -f "${PATCH}" ]]; then
  progress_log "ERROR: missing ${PATCH}"
  exit 1
fi

git_safe() {
  git -c safe.directory="${REXGLUE_SRC}" -c safe.directory='*' "$@"
}

if [[ ! -d "${REXGLUE_SRC}/.git" ]]; then
  # Full clone (not --depth 1): shallow clones break nested submodule SHAs.
  progress_run "git clone rexglue-sdk (--recurse-submodules)" \
    git clone --recurse-submodules --branch "${REXGLUE_REF}" \
      https://github.com/rexglue/rexglue-sdk.git "${REXGLUE_SRC}"
else
  progress_log "rexglue-sdk already cloned at ${REXGLUE_SRC}"
fi

cd "${REXGLUE_SRC}"

# Reset any prior patch application so re-runs are idempotent.
progress_run "git reset to ${REXGLUE_REF}" \
  bash -c 'git -c safe.directory="*" fetch origin tag "$1" 2>/dev/null || true
           git -c safe.directory="*" checkout -f "$1"
           git -c safe.directory="*" reset --hard "$1"
           git -c safe.directory="*" clean -fd' _ "${REXGLUE_REF}"

progress_step "git submodule update --init --recursive"
progress_heartbeat_start 30 "git submodules"
git_safe submodule sync --recursive
git_safe submodule update --init --recursive
progress_heartbeat_stop

progress_step "apply NHL Legacy ReXGlue patch"
if git_safe apply --check "${PATCH}"; then
  git_safe apply "${PATCH}"
  progress_done "patch applied"
else
  progress_log "ERROR: ${PATCH} does not apply cleanly to ${REXGLUE_REF}"
  progress_log "  Run: (cd ${REXGLUE_SRC} && git apply --check ${PATCH})"
  exit 1
fi

# Image toolchain is clang-20; the linux-amd64 preset hardcodes bare "clang".
if [[ -x /opt/nhl-tools/bin/clang-20 ]]; then
  CC="${CC:-/opt/nhl-tools/bin/clang-20}"
  CXX="${CXX:-/opt/nhl-tools/bin/clang++-20}"
else
  CC="${CC:-clang-20}"
  CXX="${CXX:-clang++-20}"
fi
# Prefer real paths so CMake does not require bare "clang" on PATH.
CC="$(command -v "${CC}" || true)"
CXX="$(command -v "${CXX}" || true)"
if [[ -z "${CC}" || -z "${CXX}" ]]; then
  progress_log "ERROR: clang-20 / clang++-20 not found on PATH"
  exit 1
fi
export CC CXX
export CMAKE_C_COMPILER="${CC}"
export CMAKE_CXX_COMPILER="${CXX}"
progress_log "using CC=${CC} CXX=${CXX}"

# Drop any prior configure (stale snappy config.h from failed try_compiles).
rm -rf "${REXGLUE_SRC}/out/build/linux-amd64"

# -D compiler overrides must follow --preset (preset cache sets clang/clang++).
progress_run "cmake configure rexglue (linux-amd64)" \
  cmake --preset linux-amd64 \
    -DCMAKE_C_COMPILER="${CC}" \
    -DCMAKE_CXX_COMPILER="${CXX}" \
    -DREXGLUE_USE_VULKAN=ON \
    -DREXGLUE_BUILD_TESTS=OFF \
    -DCMAKE_INSTALL_PREFIX="${REXGLUE_INSTALL}"

# Multi-config preset defaults to Debug; ship RelWithDebInfo like nhllegacy.
progress_run "cmake build rexglue install (RelWithDebInfo)" \
  cmake --build "${REXGLUE_SRC}/out/build/linux-amd64" \
    --config RelWithDebInfo \
    --target install \
    --parallel "$(nproc)"

if [[ ! -x "${REXGLUE_INSTALL}/bin/rexglue" ]]; then
  progress_log "ERROR: rexglue missing after install at ${REXGLUE_INSTALL}/bin/rexglue"
  exit 1
fi

printf '%s\n' "${STAMP_CONTENT}" > "${STAMP}"
progress_done "patched ReXGlue SDK ready"
echo "REXSDK=${REXGLUE_INSTALL}"
