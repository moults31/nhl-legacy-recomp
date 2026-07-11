#!/usr/bin/env python3
"""Pack real game assets into nhllegacy.bundle + manifest.json for WASM testing.

Usage:
  python3 tools/web/pack_bundle.py \
    --game-dir "~/code/nhl-legacy/NHL Legacy Recomp/game" \
    --out-dir ./data
"""

import argparse
import hashlib
import json
import os
import sys


GUEST_PATHS = [
    "boot.big",
    "renderboot.big",
    "audioboot.big",
    "audioboot2.big",
    "data0.big",
    "cacheboot.big",
    "default.xex",
]

GUEST_DEVICE_PREFIX = "\\Device\\Harddisk0\\Partition1"
GUEST_CACHE_PREFIX = "\\Device\\Mass0\\Cache"


def pack(game_dir: str, out_dir: str):
    bundle_path = os.path.join(out_dir, "nhllegacy.bundle")
    manifest_path = os.path.join(out_dir, "nhllegacy.manifest.json")

    os.makedirs(out_dir, exist_ok=True)

    manifest = []
    offset = 0
    total_size = 0

    print("Packing assets:")
    with open(bundle_path, "wb") as bundle:
        for guest_rel in GUEST_PATHS:
            src = os.path.join(game_dir, guest_rel.replace("\\", "/"))
            if not os.path.isfile(src):
                print(f"  WARNING: {src} not found, skipping")
                continue

            with open(src, "rb") as f:
                data = f.read()

            sha256 = hashlib.sha256(data).hexdigest()

            entry = {
                "path": guest_rel,
                "offset": offset,
                "size": len(data),
                "uncompressed_size": len(data),
                "compression": "none",
                "sha256": sha256,
            }

            bundle.write(data)
            manifest.append(entry)

            print(f"  {guest_rel:30} {len(data)/1024/1024:6.1f} MB  offset={offset}")
            offset += len(data)
            total_size += len(data)

    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)

    print(f"\nBundle  : {bundle_path}  ({total_size/1024/1024:.1f} MB)")
    print(f"Manifest: {manifest_path}  ({len(manifest)} entries)")
    print("Ready for browser testing.")


def main():
    parser = argparse.ArgumentParser(description="Pack game assets for WASM VFS")
    parser.add_argument("--game-dir", required=True, help="Path to game/ directory")
    parser.add_argument("--out-dir", default="./data", help="Output directory")
    args = parser.parse_args()

    game_dir = os.path.expanduser(args.game_dir)
    if not os.path.isdir(game_dir):
        print(f"ERROR: game directory not found: {game_dir}")
        sys.exit(1)

    pack(game_dir, args.out_dir)


if __name__ == "__main__":
    main()
