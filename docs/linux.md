# Linux and Steam Deck

You must own NHL Legacy and provide your own legally dumped copy of the game. See
[README.md](../README.md) for legal requirements.

## Build from source (native)

Compiles a native Linux `nhllegacy` ELF from this repository and your disc `.iso`.

### Prerequisites

- [Docker](https://docs.docker.com/engine/install/)
- [Steam](https://store.steampowered.com/) with **Proton 11.0** (or newer) — used only to
  run the release `nhl-legacy-builder.exe` that extracts game data from your ISO
- A legal Xbox 360 disc dump (`.iso`)
- Network access (downloads the release builder and the ReXGlue SDK)
- ~15 GB free disk space

### Build and run

```bash
ISO=/path/to/nhl-legacy.iso ./linux/build.sh
./linux/run.sh
```

`linux/build.sh`:

1. Downloads the latest release builder and extracts `out/linux/game-data/` from your ISO
2. Builds a Docker image with the pinned toolchain (cmake, ninja, clang, glslang)
3. Builds the ReXGlue SDK from source with
   [`docs/rexglue-vulkan-nhl-legacy.patch`](rexglue-vulkan-nhl-legacy.patch)
   (jersey font `exp_adjust`, signed BC5/DXN normals, readback sizing — same fixes
   as the Windows release SDK)
4. Compiles `nhllegacy` and stages `out/linux/install/`

The first build is slow (ReXGlue + game compile). Later builds reuse
`out/linux/deps/` when the patch and SDK ref are unchanged.

`linux/run.sh` launches the staged binary (ELF + ReXGlue `.so` + `game/` link).

Build log: `out/linux/build.log` (`tail -f` while a build runs).

All Linux build tooling lives under `linux/` (entry scripts, Docker image, helpers).
Windows scripts under `scripts/` are unchanged.

### Install layout

```
out/linux/
├── game-data/                 # disc assets (~10 GB)
└── install/
    ├── nhllegacy              # native ELF
    ├── librexruntimerd.so
    ├── libTracyClientrd.so
    └── game -> ../game-data
```

### Troubleshooting

| Symptom | What to try |
|---------|-------------|
| `Steam not found` / `Proton not found` | Install Steam and Proton 11.0 (or newer) |
| `default.xex does not match the supported build` | Use a vanilla disc image for the supported title build |
| `not a recognized Xbox 360 disc image` | Re-rip to a plain raw `.iso` (XDVDFS) |
| `cannot write out/linux/install` | `sudo chown -R "$USER:$USER" out` |
| Docker permission errors | Ensure your user can run `docker` (group membership or rootless) |

---

## Play a release build (Proton)

The shipped port is a **Windows executable** (`nhllegacy.exe`) with a Vulkan backend.
On Linux it runs through **Steam + Proton**.

Community testing has confirmed this works on:

- **Pop!_OS** — NVIDIA GeForce RTX 3080, driver 580.159.03, Steam (deb install), Proton 11.0
- **Steam Deck** — Proton 11.0, non-Steam shortcut to `nhllegacy.exe`

### Play an existing install

Use this when you already have a working install folder (`nhllegacy.exe` + `game/`).

1. In Steam: **Add a Game → Add a Non-Steam Game → Browse**, then select `nhllegacy.exe`.
2. Open the new library entry → **Properties → Compatibility**.
3. Enable **Force the use of a specific Steam Play compatibility tool** and choose
   **Proton 11.0**.
4. Optional launch options: `PROTON_USE_WINED3D=0 %command%` (keeps the Vulkan path;
   recommended on NVIDIA).
5. Launch from your library.

Xbox controllers generally work out of the box.

### Assemble an install from a release zip

Use this when you have a
[release zip](https://github.com/puckhead73/nhl-legacy-recomp/releases) and your own
`.iso`, but no completed install yet.

```bash
export STEAM_DIR="$HOME/.steam/debian-installation"   # adjust if needed
RELEASE=~/nhl-legacy-recomp-0.4.0
ISO=~/dumps/NHL\ Legacy.iso
OUT=~/Games/NHL\ Legacy\ Recomp

cd "$RELEASE"
"$STEAM_DIR/steamapps/common/Proton 11.0/proton" run ./nhl-legacy-builder.exe \
  install --iso "$ISO" --out "$OUT"
```

Then add `$OUT/nhllegacy.exe` to Steam as above.

The release zip contains prebuilt port binaries in `payload/`. The builder validates
your dump and lays down those binaries — it does not compile from source.

### Install folder layout (Proton)

```
NHL Legacy Recomp/
├── nhllegacy.exe
├── rexruntime.dll
├── amd_fidelityfx_vk.dll
├── nhl_enhancements.ini   # optional graphics settings
├── tunables_schema.tsv
├── logs/
└── game/                  # dumped Xbox 360 assets (~10 GB)
```

Saves and shader cache live inside the Proton prefix, typically under:

```
~/.steam/steam/steamapps/compatdata/<appid>/pfx/drive_c/users/steamuser/Documents/nhllegacy/
```

### Graphics settings

Edit `nhl_enhancements.ini` beside `nhllegacy.exe`:

```ini
supersampling=2
fullscreen=1
vsync=0
ffx_effect=cas
```

On Wayland or if fullscreen misbehaves, try `fullscreen=0`.

### Proton troubleshooting

| Symptom | What to try |
|---------|-------------|
| Game exits immediately | Confirm the shortcut targets `nhllegacy.exe` and Proton 11 is forced |
| Missing `MSVCP140.dll` / `VCRUNTIME140.dll` | Proton Experimental, or `vcrun2019` via winetricks in the prefix |
| Black screen | Set `fullscreen=0`; keep `PROTON_USE_WINED3D=0` |
| Many `cache:\` / `update:\` errors in logs | Normal — the title probes optional paths that do not exist on PC |

After each launch, check the newest `logs/nhllegacy_*.log`. A successful Linux run
shows the host Vulkan driver (for example `libGLX_nvidia.so.0` on NVIDIA), not
`nvoglv64.dll`.

### Command-line launch (without the Steam UI)

```bash
export STEAM_DIR="$HOME/.steam/debian-installation"
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM_DIR"
export STEAM_COMPAT_DATA_PATH="$STEAM_DIR/steamapps/compatdata/<your-appid>"
cd "/path/to/NHL Legacy Recomp"
PROTON_USE_WINED3D=0 "$STEAM_DIR/steamapps/common/Proton 11.0/proton" run ./nhllegacy.exe
```
