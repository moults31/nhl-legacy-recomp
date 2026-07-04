#!/usr/bin/env bash
set -euo pipefail

# Image-baked toolchain (not under /src — bind mounts must not hide it).
export NHL_TOOLS_BIN=/opt/nhl-tools/bin
export PATH="/opt/nhl-tools/bin:/usr/local/bin:/usr/bin:/bin"
export HOME="${HOME:-/tmp}"
export CC="${CC:-/opt/nhl-tools/bin/clang-20}"
export CXX="${CXX:-/opt/nhl-tools/bin/clang++-20}"
export CMAKE_C_COMPILER="${CMAKE_C_COMPILER:-${CC}}"
export CMAKE_CXX_COMPILER="${CMAKE_CXX_COMPILER:-${CXX}}"

cd /src

if [[ ! -x /opt/nhl-tools/bin/cmake || ! -x /opt/nhl-tools/bin/clang-20 ]]; then
  echo "ERROR: image toolchain missing under /opt/nhl-tools/bin." >&2
  echo "  Rebuild: docker build -f linux/docker/Dockerfile -t nhl-legacy-recomp-linux ." >&2
  exit 127
fi

exec "$@"
