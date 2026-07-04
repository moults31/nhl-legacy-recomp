# Native Linux build environment for NHL Legacy Recomp.
# Pinned: Ubuntu 24.04 + mise (linux/mise.toml) for cmake/ninja/clang.
#
# Tooling lives under /opt (not /root, not /src) so non-root `docker run -u`
# and bind-mounting the repo over /src both work.
#
# Entry: ISO=/path/to/game.iso ./linux/build.sh

FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG MISE_VERSION=2026.6.14

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl git patch unzip zip \
    build-essential g++ libstdc++-13-dev pkg-config python3 python3-venv \
    libgtk-3-dev libx11-xcb-dev libx11-dev libxcb1-dev libxkbcommon-dev \
    libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev \
    libvulkan-dev vulkan-tools glslang-tools glslang-dev \
    && rm -rf /var/lib/apt/lists/*

# glslang 14.3.0 (SDK Vulkan headers need SPIRV/SpvBuilder.h; distro 15+ dropped it).
COPY linux/docker/install_glslang.sh /tmp/install_glslang.sh
RUN chmod +x /tmp/install_glslang.sh && /tmp/install_glslang.sh && rm /tmp/install_glslang.sh

# System-wide mise + tool installs (readable/executable by any uid).
ENV MISE_DATA_DIR=/opt/mise \
    MISE_CONFIG_DIR=/opt/mise/config \
    MISE_CACHE_DIR=/opt/mise/cache \
    MISE_TRUSTED_CONFIG_PATHS=/opt/nhl-tools/mise.toml
ENV PATH="/opt/nhl-tools/bin:/opt/mise/shims:/usr/local/bin:${PATH}"

RUN curl -fsSL "https://github.com/jdx/mise/releases/download/v${MISE_VERSION}/install.sh" | \
      MISE_INSTALL_PATH=/usr/local/bin/mise sh \
    && mkdir -p /opt/mise /opt/mise/config /opt/mise/cache /opt/nhl-tools/bin \
    && chmod -R a+rX /opt/mise /opt/nhl-tools /usr/local/bin/mise

COPY linux/mise.toml /opt/nhl-tools/mise.toml
COPY linux/docker/setup_mise_toolchain.sh /opt/nhl-tools/setup_mise_toolchain.sh
COPY linux/docker/install_toolchain.sh /opt/nhl-tools/install_toolchain.sh
RUN chmod +x /opt/nhl-tools/install_toolchain.sh /opt/nhl-tools/setup_mise_toolchain.sh \
    && /opt/nhl-tools/install_toolchain.sh \
    && chmod -R a+rX /opt/mise /opt/nhl-tools

COPY linux/docker/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

WORKDIR /src
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD ["/src/linux/lib/compile.sh"]
