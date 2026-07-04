#!/usr/bin/env bash
# Build a native Linux nhllegacy from a fresh clone + legal disc ISO.
#
# Prerequisites: Docker, Steam with Proton, network access.
#
#   ISO=/path/to/nhl-legacy.iso ./linux/build.sh
#   ./linux/run.sh
set -euo pipefail

LINUX_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "${LINUX_DIR}/.." && pwd)"
cd "${ROOT}"

# shellcheck source=lib/common.sh
source "${LINUX_DIR}/lib/common.sh"

IMAGE="${NHL_DOCKER_IMAGE:-nhl-legacy-recomp-linux}"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  echo "Usage: ISO=/path/to/nhl-legacy.iso $0"
  echo "Assembles game data, compiles in Docker, stages out/linux/install/."
  echo "Then: ./linux/run.sh"
  exit 0
fi

if [[ -z "${ISO:-}" ]]; then
  echo "ERROR: set ISO to your legally obtained NHL Legacy disc dump." >&2
  echo "  ISO=/path/to/nhl-legacy.iso $0" >&2
  exit 1
fi

if ! command -v docker >/dev/null 2>&1; then
  echo "ERROR: docker not found on PATH." >&2
  exit 1
fi

echo "==> assemble game data from ISO"
ISO="${ISO}" "${LINUX_DIR}/lib/assemble.sh"

if [[ ! -f "${GAME_DATA}/default.xex" ]]; then
  echo "ERROR: assemble did not produce ${GAME_DATA}/default.xex" >&2
  exit 1
fi

echo "==> docker build ${IMAGE}"
docker build -f linux/docker/Dockerfile -t "${IMAGE}" .

# Older runs may have left root-owned out/; fix so -u $(id -u) can write.
if [[ -e "${ROOT}/out" && ! -w "${ROOT}/out" ]]; then
  echo "==> fixing ownership of out/"
  docker run --rm -u 0 \
    -v "${ROOT}:/src" \
    "${IMAGE}" \
    chown -R "$(id -u):$(id -g)" /src/out 2>/dev/null \
    || docker run --rm -u 0 \
         -v "${ROOT}:/src" \
         ubuntu:24.04 \
         chown -R "$(id -u):$(id -g)" /src/out
fi

mkdir -p "${OUT_LINUX}"

echo "==> compile in Docker"
docker run --rm \
  -u "$(id -u):$(id -g)" \
  -e HOME=/tmp \
  -v "${ROOT}:/src" \
  "${IMAGE}" \
  /src/linux/lib/compile.sh

echo "==> stage install at ${NATIVE_INSTALL}"
stage_install

echo ""
echo "Build complete."
echo "  Game data: ${GAME_DATA}"
echo "  Install:   ${NATIVE_INSTALL}"
echo "  Run:       ./linux/run.sh"
ls -lh "${NATIVE_INSTALL}/nhllegacy" "${NATIVE_INSTALL}"/*.so 2>/dev/null || true
