#!/usr/bin/env bash
# Bake mise tools + clang host-link wrappers into the image under /opt.
# Runs at docker build time as root; results must be world-readable.
set -euo pipefail

export MISE_DATA_DIR="${MISE_DATA_DIR:-/opt/mise}"
export MISE_CONFIG_DIR="${MISE_CONFIG_DIR:-/opt/mise/config}"
export MISE_CACHE_DIR="${MISE_CACHE_DIR:-/opt/mise/cache}"
export MISE_TRUSTED_CONFIG_PATHS="${MISE_TRUSTED_CONFIG_PATHS:-/opt/nhl-tools/mise.toml}"

cd /opt/nhl-tools
# mise install reads .mise.toml from cwd — symlink the pinned config.
ln -sfn /opt/nhl-tools/mise.toml /opt/nhl-tools/.mise.toml
mise trust /opt/nhl-tools/.mise.toml
mise install

export NHL_TOOLS_BIN=/opt/nhl-tools/bin
# shellcheck source=setup_mise_toolchain.sh
source /opt/nhl-tools/setup_mise_toolchain.sh /opt/nhl-tools

# Real binaries in /opt/nhl-tools/bin — no mise shims at runtime (shims require
# trusting bind-mounted /src, which non-root cannot do).
ln -sfn "$(mise which cmake)" /opt/nhl-tools/bin/cmake
ln -sfn "$(mise which ninja)" /opt/nhl-tools/bin/ninja

command -v cmake
command -v ninja
command -v clang-20
command -v clang++-20
cmake --version | head -1
clang-20 --version | head -1
env -i PATH="/opt/nhl-tools/bin:/usr/bin:/bin" HOME=/tmp \
  cmake --version | head -1
env -i PATH="/opt/nhl-tools/bin:/usr/bin:/bin" HOME=/tmp \
  ninja --version
