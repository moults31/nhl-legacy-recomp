#!/usr/bin/env python3
"""
NHL Legacy WASM — game asset extraction & bundle packer.

Two modes:

  1. extract  <game.iso>  <out_dir/>   — extract boot assets from the game ISO
                                        and pack them into a .bundle file.
  2. test     <out_dir/>                — generate a minimal test bundle with
                                        dummy files for browser testing.

The .bundle file is a concatenation of (optionally compressed) game files,
accompanied by a JSON manifest that maps each logical guest path to its
offset, size, compression type, and hash within the bundle.

The server should serve the .bundle at /data/nhllegacy.bundle and the
manifest at /data/nhllegacy.manifest.json.  The browser launcher fetches
the manifest at boot and reads chunks via HTTP Range requests.
"""

import argparse
import hashlib
import json
import os
import struct
import sys


# ---------------------------------------------------------------------------
#  Test bundle — creates dummy data for VFS pipeline testing
# ---------------------------------------------------------------------------

def make_test_bundle(out_dir: str):
    """Generate a minimal .bundle + manifest for browser testing."""
    bundle_path = os.path.join(out_dir, "nhllegacy.bundle")
    manifest_path = os.path.join(out_dir, "nhllegacy.manifest.json")

    # Dummy files that the guest tries to open during boot
    entries = {
        # Minimal valid XEX-like header (just enough to pass basic parsing)
        "fe\\ion\\mainmenu.bin": bytes(0x4000),
        "rendering\\player\\texlib_0.rx2": bytes(0x800),
        "audio\\boot\\audio_boot.bnk": bytes(0x1000),
        "data\\boot\\boot.big": bytes(0x1000),
    }

    manifest = []
    offset = 0
    with open(bundle_path, "wb") as bundle:
        for path, data in sorted(entries.items()):
            sha = hashlib.sha256(data).hexdigest()
            bundle.write(data)
            manifest.append({
                "path": path.replace("/", "\\"),  # guest uses backslashes
                "offset": offset,
                "size": len(data),
                "uncompressed_size": len(data),
                "compression": "none",
                "sha256": sha,
            })
            offset += len(data)

    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)

    print(f"  Bundle  : {bundle_path}  ({offset/1024:.1f} KB)")
    print(f"  Manifest: {manifest_path}  ({len(entries)} entries)")
    print(f"  Status  : Ready for browser testing — start serve.py and reload.")


# ---------------------------------------------------------------------------
#  Extraction helpers (require game ISO)
# ---------------------------------------------------------------------------

def extract_from_iso(iso_path: str, out_dir: str):
    """Extract boot assets from the NHL Legacy game ISO.

    TODO: Implement when game ISO is available.
    Requires XDVDFS parsing and EA .big archive extraction.
    See `tools/packager/src/` for the C++ reference implementation.
    """
    raise NotImplementedError(
        "Game ISO extraction requires the rexglue SDK's DiscImageDevice.\n"
        "Run this script with `--test <dir>` to generate a test bundle "
        "for browser testing without game assets.")


# ---------------------------------------------------------------------------
#  CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="NHL Legacy WASM — game asset extractor & bundle packer")
    parser.add_argument("--test", metavar="OUT_DIR",
                        help="Generate a minimal test bundle (no game ISO required)")
    parser.add_argument("--extract", metavar="GAME_ISO",
                        help="Path to the NHL Legacy game ISO")
    parser.add_argument("--out", metavar="DIR", default="./data",
                        help="Output directory for .bundle and .manifest (default: ./data)")

    args = parser.parse_args()

    if args.test:
        print("Generating test bundle...")
        os.makedirs(args.test, exist_ok=True)
        make_test_bundle(args.test)
        return

    if args.extract:
        print(f"Extracting from ISO: {args.extract}")
        os.makedirs(args.out, exist_ok=True)
        extract_from_iso(args.extract, args.out)
        return

    parser.print_help()


if __name__ == "__main__":
    main()
