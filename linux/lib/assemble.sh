#!/usr/bin/env bash
# Assemble out/linux/game-data/ from a legal NHL Legacy .iso.
# Called by linux/build.sh — not a user entry point.
set -euo pipefail

LINUX_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "${LINUX_DIR}/.." && pwd)"
# shellcheck source=common.sh
source "${LINUX_DIR}/lib/common.sh"

BUILDER_STAGING="${OUT_LINUX}/builder-staging"
RELEASE_REPO="${NHL_RELEASE_REPO:-puckhead73/nhl-legacy-recomp}"
RELEASE_CACHE_ROOT="${LINUX_CACHE}/nhl-release"

if [[ -z "${ISO:-}" ]]; then
  echo "ERROR: set ISO to your legally obtained NHL Legacy disc dump." >&2
  exit 1
fi
if [[ ! -f "${ISO}" ]]; then
  echo "ERROR: ISO not found: ${ISO}" >&2
  exit 1
fi
ISO="$(cd "$(dirname "${ISO}")" && pwd)/$(basename "${ISO}")"

if [[ -f "${GAME_DATA}/default.xex" ]]; then
  echo "game data already present at ${GAME_DATA}"
  exit 0
fi

find_steam_dir() {
  local d
  for d in \
    "${STEAM_DIR:-}" \
    "${HOME}/.steam/debian-installation" \
    "${HOME}/.steam/steam" \
    "${HOME}/.local/share/Steam" \
    "${HOME}/.var/app/com.valvesoftware.Steam/data/Steam"; do
    if [[ -n "${d}" && -d "${d}/steamapps" ]]; then
      echo "${d}"
      return 0
    fi
  done
  return 1
}

find_proton() {
  local steam="$1"
  local d best=""
  while IFS= read -r d; do
    [[ -x "${d}/proton" ]] || continue
    best="${d}"
  done < <(find "${steam}/steamapps/common" -maxdepth 1 -type d -name 'Proton*' 2>/dev/null | sort -V)
  if [[ -n "${best}" ]]; then
    echo "${best}"
    return 0
  fi
  return 1
}

download_release_builder() {
  local api_url json tag zip_url cache zip extract top
  api_url="https://api.github.com/repos/${RELEASE_REPO}/releases/latest"
  echo "==> downloading release builder from ${RELEASE_REPO}"
  json="$(curl -fsSL "${api_url}")"
  tag="$(printf '%s' "${json}" | python3 -c 'import json,sys; print(json.load(sys.stdin)["tag_name"])')"
  zip_url="$(printf '%s' "${json}" | python3 -c '
import json, sys
assets = json.load(sys.stdin).get("assets") or []
for a in assets:
    name = a.get("name") or ""
    if name.endswith(".zip") and "nhl-legacy-recomp" in name:
        print(a["browser_download_url"])
        break
else:
    sys.exit(1)
')"
  cache="${RELEASE_CACHE_ROOT}/${tag#v}"
  zip="${RELEASE_CACHE_ROOT}/nhl-legacy-recomp-${tag#v}.zip"

  mkdir -p "${RELEASE_CACHE_ROOT}"
  if [[ ! -f "${zip}" ]]; then
    curl -fL --retry 3 -o "${zip}.partial" "${zip_url}"
    mv "${zip}.partial" "${zip}"
  fi

  if [[ ! -f "${cache}/nhl-legacy-builder.exe" ]]; then
    extract="${cache}.extract"
    rm -rf "${extract}" "${cache}"
    mkdir -p "${extract}" "${cache}"
    # Release zips are built on Windows (backslash path separators). Info-ZIP
    # still extracts correctly but exits 1 with a warning; only fail on real errors.
    unzip -q "${zip}" -d "${extract}" || {
      local rc=$?
      if [[ "${rc}" -ne 1 ]]; then
        echo "ERROR: unzip failed (${rc}): ${zip}" >&2
        exit 1
      fi
    }
    if [[ -f "${extract}/nhl-legacy-builder.exe" ]]; then
      mv "${extract}"/* "${cache}/"
    else
      top="$(find "${extract}" -name nhl-legacy-builder.exe | head -1)"
      if [[ -z "${top}" ]]; then
        echo "ERROR: nhl-legacy-builder.exe not found inside ${zip}" >&2
        exit 1
      fi
      mv "$(dirname "${top}")"/* "${cache}/"
    fi
    rm -rf "${extract}"
  fi

  if [[ ! -f "${cache}/nhl-legacy-builder.exe" ]]; then
    echo "ERROR: nhl-legacy-builder.exe missing after extracting ${zip}" >&2
    exit 1
  fi
  RELEASE_DIR="${cache}"
}

STEAM_DIR="$(find_steam_dir)" || {
  echo "ERROR: Steam not found. Install Steam + Proton (needed to run nhl-legacy-builder.exe)." >&2
  exit 1
}
PROTON_DIR="$(find_proton "${STEAM_DIR}")" || {
  echo "ERROR: Proton not found under ${STEAM_DIR}/steamapps/common/." >&2
  echo "  Install Proton 11.0 (or newer) in Steam." >&2
  exit 1
}
download_release_builder

echo "==> assembling game data from ISO"
echo "    ISO=${ISO}"
echo "    RELEASE_DIR=${RELEASE_DIR}"
echo "    PROTON=${PROTON_DIR}"
echo "    OUT=${GAME_DATA}"

mkdir -p "${OUT_LINUX}" "${LINUX_CACHE}/proton-builder-prefix"
rm -rf "${BUILDER_STAGING}"
mkdir -p "${BUILDER_STAGING}"

export STEAM_COMPAT_CLIENT_INSTALL_PATH="${STEAM_DIR}"
export STEAM_COMPAT_DATA_PATH="${LINUX_CACHE}/proton-builder-prefix"

(
  cd "${RELEASE_DIR}"
  "${PROTON_DIR}/proton" run ./nhl-legacy-builder.exe \
    install --iso "${ISO}" --out "${BUILDER_STAGING}"
)

if [[ ! -f "${BUILDER_STAGING}/game/default.xex" ]]; then
  echo "ERROR: builder did not produce game/default.xex under ${BUILDER_STAGING}" >&2
  exit 1
fi

rm -rf "${GAME_DATA}"
mv "${BUILDER_STAGING}/game" "${GAME_DATA}"
rm -rf "${BUILDER_STAGING}"

echo "game data ready: ${GAME_DATA}"
ls -lh "${GAME_DATA}/default.xex"
