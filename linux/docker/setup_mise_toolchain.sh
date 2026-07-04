#!/usr/bin/env bash
# Bake mise-pinned cmake/ninja/clang wrappers for the Docker image.
# Used only by linux/docker/install_toolchain.sh at image build time.
#
# mise's clang is a conda-forge build whose default config sets
#   --sysroot=<conda>/x86_64-conda-linux-gnu/sysroot
# That sysroot hides distro libgcc_s / libstdc++. Wrappers force a normal
# host link against the image g++:
#   --gcc-install-dir=<dir from g++>
#   --sysroot=/
#   --ld-path=/usr/bin/ld  (link steps only)
set -euo pipefail

ROOT="${1:-/opt/nhl-tools}"

if [[ -x /usr/local/bin/mise ]]; then
  export PATH="/usr/local/bin:${PATH}"
fi
if [[ -d /opt/mise ]]; then
  export MISE_DATA_DIR="${MISE_DATA_DIR:-/opt/mise}"
  export MISE_CONFIG_DIR="${MISE_CONFIG_DIR:-/opt/mise/config}"
  export MISE_CACHE_DIR="${MISE_CACHE_DIR:-/opt/mise/cache}"
fi

TOOLS_BIN="${NHL_TOOLS_BIN:-/opt/nhl-tools/bin}"

if [[ -x "${TOOLS_BIN}/clang-20" && -x "${TOOLS_BIN}/clang++-20" ]] \
   && [[ -x "${TOOLS_BIN}/cmake" || -x /opt/nhl-tools/bin/cmake ]]; then
  export PATH="${TOOLS_BIN}:${PATH}"
  export CC="${CC:-${TOOLS_BIN}/clang-20}"
  export CXX="${CXX:-${TOOLS_BIN}/clang++-20}"
  export CMAKE_C_COMPILER="${CMAKE_C_COMPILER:-${CC}}"
  export CMAKE_CXX_COMPILER="${CMAKE_CXX_COMPILER:-${CXX}}"
  return 0 2>/dev/null || exit 0
fi

if ! command -v mise >/dev/null 2>&1; then
  echo "ERROR: mise not found (expected during docker image build)." >&2
  exit 1
fi

MISE_TOML="${ROOT}/mise.toml"
if [[ ! -f "${MISE_TOML}" ]]; then
  MISE_TOML="${ROOT}/.mise.toml"
fi
if [[ ! -f "${MISE_TOML}" ]]; then
  MISE_TOML=/opt/nhl-tools/mise.toml
fi
if [[ ! -f "${MISE_TOML}" ]]; then
  echo "ERROR: mise.toml not found at ${MISE_TOML}" >&2
  exit 1
fi

export MISE_TRUSTED_CONFIG_PATHS="${MISE_TRUSTED_CONFIG_PATHS:-${MISE_TOML}}"
eval "$(mise activate bash)"
mise install

if [[ ! -x /usr/bin/g++ ]]; then
  echo "ERROR: host g++ not found at /usr/bin/g++" >&2
  exit 1
fi
if [[ ! -x /usr/bin/ld ]]; then
  echo "ERROR: /usr/bin/ld not found" >&2
  exit 1
fi

GCC_LIB="$(env -u LIBRARY_PATH -u LD_LIBRARY_PATH /usr/bin/g++ -print-file-name=libstdc++.so)"
GCC_INSTALL_DIR="$(cd "$(dirname "${GCC_LIB}")" && pwd)"
if [[ ! -f "${GCC_INSTALL_DIR}/libstdc++.so" ]]; then
  echo "ERROR: could not resolve g++ libstdc++ dir (got ${GCC_INSTALL_DIR})." >&2
  exit 1
fi

CLANG_BIN="$(mise where clang)/bin"
REAL_CLANG="${CLANG_BIN}/clang-20"
if [[ ! -x "${REAL_CLANG}" ]]; then
  echo "ERROR: real clang-20 binary missing at ${REAL_CLANG}" >&2
  exit 1
fi

mkdir -p "${TOOLS_BIN}"
# Host-link flags for conda clang. Only pass the system linker when actually
# linking: compile-only try_compile checks (e.g. snappy with -Werror) must not
# see unused/deprecated linker flags or every SIMD/builtin feature mis-detects.
# Clang 20 wants --ld-path=PATH ( -fuse-ld=PATH is -Werror,-Wfuse-ld-path ).
CLANG_HOST_FLAGS="--gcc-install-dir=${GCC_INSTALL_DIR} --sysroot=/"

write_clang_wrapper() {
  local name="$1"
  local argv0="${2:-$1}"
  local dest="${TOOLS_BIN}/${name}"
  rm -f "${dest}"
  cat > "${dest}" << EOF
#!/usr/bin/env bash
link=1
for arg in "\$@"; do
  case "\$arg" in
    -c|-E|-S|-fsyntax-only|--preprocess|--assemble) link=0 ;;
  esac
done
if [[ "\${link}" -eq 1 ]]; then
  exec -a "${argv0}" "${REAL_CLANG}" ${CLANG_HOST_FLAGS} --ld-path=/usr/bin/ld "\$@"
else
  exec -a "${argv0}" "${REAL_CLANG}" ${CLANG_HOST_FLAGS} "\$@"
fi
EOF
  chmod +x "${dest}"
}

write_clang_wrapper clang-20 clang
write_clang_wrapper clang-cpp clang-cpp
write_clang_wrapper clang++-20 clang++

_smoke="/tmp/nhl-clang-smoke.$$"
if ! env -i PATH="/usr/bin:/bin" HOME="${HOME:-/tmp}" \
    "${TOOLS_BIN}/clang-20" -x c - -o "${_smoke}.c" <<< 'int main(void){return 0;}' \
    2>/dev/null; then
  echo "ERROR: clang C host-link smoke test failed." >&2
  exit 1
fi
if ! env -i PATH="/usr/bin:/bin" HOME="${HOME:-/tmp}" \
    "${TOOLS_BIN}/clang++-20" -x c++ - -o "${_smoke}.cxx" <<< 'int main(){return 0;}' \
    2>/dev/null; then
  echo "ERROR: clang++ C++ host-link smoke test failed." >&2
  exit 1
fi
rm -f "${_smoke}.c" "${_smoke}.cxx"

export PATH="${TOOLS_BIN}:${PATH}"
export CC="${CC:-${TOOLS_BIN}/clang-20}"
export CXX="${CXX:-${TOOLS_BIN}/clang++-20}"
export CMAKE_C_COMPILER="${CMAKE_C_COMPILER:-${CC}}"
export CMAKE_CXX_COMPILER="${CMAKE_CXX_COMPILER:-${CXX}}"
