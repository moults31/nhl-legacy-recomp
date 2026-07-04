#!/usr/bin/env bash
# Shared paths, progress logging, and install staging for the native Linux build.
#
# Layout under out/linux/:
#   game-data/          disc assets (from ISO)
#   install/nhllegacy   ELF (RPATH $ORIGIN)
#   install/*.so        ReXGlue runtime
#   install/game -> ../game-data
#   build/              cmake build tree
#   deps/               NHL-patched ReXGlue SDK (built from source)
#   cache/              release builder + Proton prefix

if [[ -z "${LINUX_DIR:-}" ]]; then
  LINUX_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi
if [[ -z "${ROOT:-}" ]]; then
  ROOT="$(cd "${LINUX_DIR}/.." && pwd)"
fi

: "${OUT_LINUX:=${ROOT}/out/linux}"
: "${NATIVE_INSTALL:=${OUT_LINUX}/install}"
: "${BUILD_DIR:=${OUT_LINUX}/build}"
: "${GAME_DATA:=${OUT_LINUX}/game-data}"
: "${REXGLUE_INSTALL:=${OUT_LINUX}/deps/rexglue-sdk-install/linux-amd64}"
: "${LINUX_CACHE:=${OUT_LINUX}/cache}"
: "${LOG_FILE:=${OUT_LINUX}/build.log}"

_PROGRESS_HEARTBEAT_PID=""

progress_log() {
  local msg="[$(date '+%Y-%m-%d %H:%M:%S')] $*"
  echo "${msg}" >&2
  if [[ -n "${LOG_FILE:-}" ]]; then
    mkdir -p "$(dirname "${LOG_FILE}")" 2>/dev/null || true
    echo "${msg}" >> "${LOG_FILE}" 2>/dev/null || true
  fi
}

progress_step() {
  progress_log "==> $*"
}

progress_done() {
  progress_log "<== done: $*"
}

progress_heartbeat_start() {
  local interval="${1:-60}"
  local message="${2:-working}"
  progress_heartbeat_stop 2>/dev/null || true
  (
    local n=0
    while true; do
      sleep "${interval}"
      n=$((n + 1))
      progress_log "heartbeat #${n} (${interval}s): ${message}"
    done
  ) &
  _PROGRESS_HEARTBEAT_PID=$!
}

progress_heartbeat_stop() {
  if [[ -n "${_PROGRESS_HEARTBEAT_PID:-}" ]]; then
    kill "${_PROGRESS_HEARTBEAT_PID}" 2>/dev/null || true
    wait "${_PROGRESS_HEARTBEAT_PID}" 2>/dev/null || true
    _PROGRESS_HEARTBEAT_PID=""
  fi
}

progress_run() {
  local message="$1"
  shift
  progress_step "${message}"
  progress_heartbeat_start 45 "${message}"
  if "$@"; then
    progress_heartbeat_stop
    progress_done "${message}"
    return 0
  else
    local rc=$?
    progress_heartbeat_stop
    progress_log "FAILED (${rc}): ${message}"
    return "${rc}"
  fi
}

progress_cmake_build() {
  local build_dir="$1"
  local target="${2:-install}"
  progress_step "cmake --build ${build_dir} --target ${target}"
  progress_heartbeat_start 60 "cmake build ${target}"
  if cmake --build "${build_dir}" --target "${target}" --parallel "$(nproc)"; then
    progress_heartbeat_stop
    progress_done "cmake build ${target}"
    return 0
  else
    local rc=$?
    progress_heartbeat_stop
    progress_log "FAILED (${rc}): cmake build ${target}"
    return "${rc}"
  fi
}

# Copy ELF + runtime .so + game symlink into NATIVE_INSTALL.
stage_install() {
  local _bin="${1:-${BUILD_DIR}/nhllegacy}"
  local _sdk_lib _soname

  if [[ ! -x "${_bin}" ]]; then
    echo "ERROR: native binary not found at ${_bin}" >&2
    echo "  Build first: ISO=/path/to/nhl-legacy.iso ./linux/build.sh" >&2
    return 1
  fi

  if [[ ! -f "${GAME_DATA}/default.xex" ]]; then
    echo "ERROR: game data missing at ${GAME_DATA}" >&2
    echo "  Build first: ISO=/path/to/nhl-legacy.iso ./linux/build.sh" >&2
    return 1
  fi

  if [[ ! -d "${REXGLUE_INSTALL}/lib" ]]; then
    echo "ERROR: ReXGlue SDK lib dir not found at ${REXGLUE_INSTALL}/lib" >&2
    return 1
  fi

  mkdir -p "${NATIVE_INSTALL}"
  install -m 755 "${_bin}" "${NATIVE_INSTALL}/nhllegacy"

  _sdk_lib="${REXGLUE_INSTALL}/lib"
  for _soname in librexruntimerd.so libTracyClientrd.so \
                 librexruntime.so libTracyClient.so \
                 librexruntimed.so libTracyClientd.so; do
    if [[ -f "${_sdk_lib}/${_soname}" ]]; then
      cp -a "${_sdk_lib}/${_soname}" "${NATIVE_INSTALL}/"
    fi
  done

  ln -sfn "../game-data" "${NATIVE_INSTALL}/game"

  if [[ ! -x "${NATIVE_INSTALL}/nhllegacy" ]]; then
    echo "ERROR: staged binary missing at ${NATIVE_INSTALL}/nhllegacy" >&2
    return 1
  fi
  if [[ ! -e "${NATIVE_INSTALL}/librexruntimerd.so" && ! -e "${NATIVE_INSTALL}/librexruntime.so" ]]; then
    echo "ERROR: runtime .so not staged into ${NATIVE_INSTALL}" >&2
    return 1
  fi
  if [[ ! -f "${NATIVE_INSTALL}/game/default.xex" ]]; then
    echo "ERROR: ${NATIVE_INSTALL}/game/default.xex missing" >&2
    return 1
  fi
}
