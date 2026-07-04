#!/usr/bin/env bash
# Compile nhllegacy inside the Docker image (image-baked toolchain only).
# Invoked by linux/build.sh — not a user entry point.
set -euo pipefail

LINUX_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "${LINUX_DIR}/.." && pwd)"
# shellcheck source=common.sh
source "${LINUX_DIR}/lib/common.sh"

cd "${ROOT}"

export REXGLUE_REF="${REXGLUE_REF:-v0.8.0}"
export CC="${CC:-clang-20}"
export CXX="${CXX:-clang++-20}"

progress_step "native Linux Vulkan build"

if [[ ! -x /opt/nhl-tools/bin/clang-20 || ! -x /opt/nhl-tools/bin/cmake ]]; then
  progress_log "ERROR: image toolchain missing under /opt/nhl-tools/bin"
  progress_log "  Rebuild: docker build -f linux/docker/Dockerfile -t nhl-legacy-recomp-linux ."
  exit 1
fi

progress_log "clang=$(clang-20 --version | head -1)"
progress_log "cmake=$(cmake --version | head -1)"
progress_log "logging to ${LOG_FILE}"

mkdir -p "${OUT_LINUX}"

if [[ ! -f "${GAME_DATA}/default.xex" ]]; then
  progress_log "ERROR: default.xex not found at ${GAME_DATA}/default.xex"
  exit 1
fi

eval "$("${LINUX_DIR}/lib/rexglue.sh" | grep '^REXSDK=')"
REXGLUE_INSTALL="${REXSDK:-${REXGLUE_INSTALL}}"

if [[ ! -x "${REXGLUE_INSTALL}/bin/rexglue" ]]; then
  progress_log "ERROR: rexglue not found at ${REXGLUE_INSTALL}/bin/rexglue"
  exit 1
fi
if [[ ! -f "${REXGLUE_INSTALL}/.nhl-legacy-patched" ]]; then
  progress_log "ERROR: ReXGlue install is not NHL-patched (${REXGLUE_INSTALL})"
  exit 1
fi

export PATH="${REXGLUE_INSTALL}/bin:${PATH}"
export REXSDK="${REXGLUE_INSTALL}"
progress_log "REXGLUE_INSTALL=${REXGLUE_INSTALL}"

GAME_ROOT="${NATIVE_INSTALL}/game"
ENTRYPOINT_XEX="${GAME_ROOT}/default.xex"

mkdir -p "${NATIVE_INSTALL}"
ln -sfn "../game-data" "${NATIVE_INSTALL}/game"
if [[ ! -f "${ENTRYPOINT_XEX}" ]]; then
  progress_log "ERROR: ${ENTRYPOINT_XEX} missing"
  exit 1
fi

if [[ ! -f "${ROOT}/generated/rexglue.cmake" ]]; then
  progress_step "rexglue codegen"
  CODEGEN_MANIFEST="${ROOT}/nhllegacy_manifest.codegen.toml"
  cp "${ROOT}/nhllegacy_manifest.toml" "${CODEGEN_MANIFEST}"
  python3 - << PY
from pathlib import Path
import re
m = Path("${CODEGEN_MANIFEST}")
t = m.read_text()
t = re.sub(r'game_root = ".*?"', 'game_root = "${GAME_ROOT}"', t)
t = re.sub(r'file_path = ".*?"', 'file_path = "${ENTRYPOINT_XEX}"', t)
m.write_text(t)
PY
  progress_heartbeat_start 30 "rexglue codegen (can take several minutes)"
  rexglue codegen --force "${CODEGEN_MANIFEST}"
  progress_heartbeat_stop
  progress_done "rexglue codegen"
else
  progress_log "generated/ already present — skipping codegen"
fi

progress_run "cmake configure nhllegacy (linux-amd64-vk)" \
  cmake -S "${ROOT}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER="${CC}" \
    -DCMAKE_CXX_COMPILER="${CXX}" \
    -DCMAKE_PREFIX_PATH="${REXGLUE_INSTALL}" \
    -DNHLLEGACY_VULKAN_BACKEND=ON \
    -DNHLLEGACY_VULKAN_ONLY=ON \
    -DNHLLEGACY_BUILD_PACKAGER=OFF \
    -DNHLLEGACY_BUILD_TRACE_TOOLS=OFF

progress_cmake_build "${BUILD_DIR}" nhllegacy

BIN="${BUILD_DIR}/nhllegacy"
progress_step "staging ${NATIVE_INSTALL}"
stage_install "${BIN}"

progress_done "native Linux build"
echo ""
echo "Built:  ${BIN}"
echo "Install:${NATIVE_INSTALL}"
ls -lh "${NATIVE_INSTALL}/nhllegacy" "${NATIVE_INSTALL}"/*.so 2>/dev/null || true
