#!/usr/bin/env bash
# Run the native Linux build staged by linux/build.sh.
#
#   ./linux/run.sh
#   ./linux/run.sh -- [args for nhllegacy...]
set -euo pipefail

LINUX_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "${LINUX_DIR}/.." && pwd)"
# shellcheck source=lib/common.sh
source "${LINUX_DIR}/lib/common.sh"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  echo "Usage: $0 [--] [args passed to nhllegacy...]"
  exit 0
fi
if [[ "${1:-}" == "--" ]]; then
  shift
fi

if [[ ! -f "${GAME_DATA}/default.xex" ]]; then
  echo "ERROR: game data missing at ${GAME_DATA}" >&2
  echo "  ISO=/path/to/nhl-legacy.iso ./linux/build.sh" >&2
  exit 1
fi

if [[ ! -w "$(dirname "${NATIVE_INSTALL}")" ]] && [[ ! -w "${NATIVE_INSTALL}" ]]; then
  echo "ERROR: cannot write ${NATIVE_INSTALL}." >&2
  echo "  One-time fix: sudo chown -R \"\$USER:\$USER\" \"${ROOT}/out\"" >&2
  exit 1
fi

stage_install

cd "${NATIVE_INSTALL}"
exec ./nhllegacy "$@"
