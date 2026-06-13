#!/usr/bin/env python3
"""Offline decoder for high-cut path-C draw packets (highcut_frame_<N>.bin / highcut_p3_draw.bin).

This is the proven C-5 debugging tool: parse the self-describing binary packet the CommandProcessor
writes per owned draw (gpu/hooks/highcut_draw_packet.h), print each draw's decoded state, and optionally
decode its sampled texture(s) to PNG (stdlib BCn decoder + zlib PNG encoder) so you can eyeball the
real guest texels without a GPU. It cracked the swizzle/font/mask bugs in C-5a/b; for C-5c it shows the
per-draw depth/stencil/cull state (v4 header) — e.g. whether the menu "green bar" draws are
stencil-tested with a compare that should fail vs the cleared stencil.

Usage:
    # summarize a whole captured frame (reads highcut_frame.count + highcut_frame_*.bin in <dir>)
    python tools/highcut_packet_decode.py <build_dir>
    # one packet, with per-draw detail
    python tools/highcut_packet_decode.py <build_dir>/highcut_frame_42.bin --verbose
    # also decode each draw's textures to PNG next to the .bin
    python tools/highcut_packet_decode.py <build_dir>/highcut_frame_42.bin --png

The struct layout MUST track gpu/hooks/highcut_draw_packet.h. v4 header (kDrawPacketVersion=4):
    14 u32  magic..ps_sampler_count
     6 f32  vp_x vp_y vp_w vp_h vp_zmin vp_zmax
     8 u32  blend_enable blend_src blend_dst blend_op blend_src_a blend_dst_a blend_op_a color_write_mask
     4 u32  sc_left sc_top sc_right sc_bottom
    17 u32  depth_enable depth_write depth_func stencil_enable stencil_read_mask stencil_write_mask
            stencil_ref front_fail front_pass front_depthfail front_func back_fail back_pass
            back_depthfail back_func cull_mode front_ccw
"""
import argparse
import os
import struct
import sys
import zlib

MAGIC = 0x48334450  # 'H3DP'

HDR_V3 = "<14I6f8I4I"          # 14 u32 + 6 f32 + 8 u32 + 4 u32
HDR_V4 = HDR_V3 + "17I"        # + 17 u32 (depth/stencil/cull)
HDR_V5 = HDR_V4 + "2I"         # + 2 u32 (index_format, index_bytes)
HDR_V3_FIELDS = [
    "magic", "version", "vertex_count", "topology", "fetch_bytes", "sys_bytes", "shared_bytes",
    "bool_bytes", "vs_float_bytes", "ps_float_bytes", "vs_spirv_bytes", "ps_spirv_bytes",
    "texture_count", "ps_sampler_count",
    "vp_x", "vp_y", "vp_w", "vp_h", "vp_zmin", "vp_zmax",
    "blend_enable", "blend_src", "blend_dst", "blend_op", "blend_src_a", "blend_dst_a",
    "blend_op_a", "color_write_mask",
    "sc_left", "sc_top", "sc_right", "sc_bottom",
]
HDR_V4_FIELDS = HDR_V3_FIELDS + [
    "depth_enable", "depth_write", "depth_func", "stencil_enable", "stencil_read_mask",
    "stencil_write_mask", "stencil_ref", "front_fail_op", "front_pass_op", "front_depth_fail_op",
    "front_func", "back_fail_op", "back_pass_op", "back_depth_fail_op", "back_func", "cull_mode",
    "front_ccw",
]
HDR_V5_FIELDS = HDR_V4_FIELDS + ["index_format", "index_bytes"]
TEX_DESC = "<8I"  # width height tex_format row_pitch_bytes data_bytes fetch_slot is_signed swizzle
TEX_FIELDS = ["width", "height", "tex_format", "row_pitch_bytes", "data_bytes", "fetch_slot",
              "is_signed", "swizzle"]

CMP = {0: "Never", 1: "Less", 2: "Equal", 3: "LEqual", 4: "Greater", 5: "NotEqual",
       6: "GEqual", 7: "Always"}
SOP = {0: "Keep", 1: "Zero", 2: "Replace", 3: "IncClamp", 4: "DecClamp", 5: "Invert",
       6: "IncWrap", 7: "DecWrap"}
TOPO = {0: "TriList", 1: "TriStrip", 2: "QuadExpand"}
TEXFMT = {0: "RGBA8", 1: "BC1", 2: "BC2", 3: "BC3"}
CULL = {0: "none", 1: "front", 2: "back"}


def parse_header(buf):
    if len(buf) < 8 or struct.unpack_from("<I", buf, 0)[0] != MAGIC:
        return None, 0, None
    version = struct.unpack_from("<I", buf, 4)[0]
    # Only v3 (C-5a), v4 (C-5c), v5 (C-5d) share the layout this tool decodes; v2 and earlier had a
    # different header and would parse to garbage, so reject them explicitly rather than print nonsense.
    if version == 5:
        fmt, fields = HDR_V5, HDR_V5_FIELDS
    elif version == 4:
        fmt, fields = HDR_V4, HDR_V4_FIELDS
    elif version == 3:
        fmt, fields = HDR_V3, HDR_V3_FIELDS
    else:
        return None, 0, version
    size = struct.calcsize(fmt)
    if len(buf) < size:
        return None, 0, version
    vals = struct.unpack_from(fmt, buf, 0)
    return dict(zip(fields, vals)), size, version


def fmt_draw_line(h):
    s = (f"v{h['version']} verts={h['vertex_count']:<5} {TOPO.get(h['topology'],'?'):10} "
         f"tex={h['texture_count']} cwm=0x{h['color_write_mask']:X} "
         f"blend={'on' if h['blend_enable'] else 'off'}(s{h['blend_src']}/d{h['blend_dst']}/o{h['blend_op']}) "
         f"vp=({h['vp_x']:.0f},{h['vp_y']:.0f},{h['vp_w']:.0f},{h['vp_h']:.0f})")
    if h["version"] >= 4:
        depth = (f"depth={'EN' if h['depth_enable'] else '--'}"
                 f"{'W' if h['depth_write'] else '-'}/{CMP.get(h['depth_func'],'?')}")
        sten = "stencil=--"
        if h["stencil_enable"]:
            sten = (f"stencil=EN ref={h['stencil_ref']} rd=0x{h['stencil_read_mask']:X} "
                    f"wr=0x{h['stencil_write_mask']:X} f.cmp={CMP.get(h['front_func'],'?')} "
                    f"f.ops[{SOP.get(h['front_fail_op'],'?')},{SOP.get(h['front_pass_op'],'?')},"
                    f"{SOP.get(h['front_depth_fail_op'],'?')}]")
        cull = f"cull={CULL.get(h['cull_mode'],'?')} {'ccw' if h['front_ccw'] else 'cw'}"
        s += f"  {depth}  {sten}  {cull}"
    if h["version"] >= 5:
        ifmt = {0: "none", 1: "u16", 2: "u32"}.get(h["index_format"], "?")
        s += f"  idx={ifmt}" + (f"/{h['index_bytes']}B" if h["index_format"] else "")
    return s


def iter_textures(buf, h, hdr_size):
    off = (hdr_size + h["fetch_bytes"] + h["sys_bytes"] + h["shared_bytes"] + h["bool_bytes"] +
           h["vs_float_bytes"] + h["ps_float_bytes"] + h["vs_spirv_bytes"] + h["ps_spirv_bytes"])
    for _ in range(h["texture_count"]):
        if off + struct.calcsize(TEX_DESC) > len(buf):
            return
        d = dict(zip(TEX_FIELDS, struct.unpack_from(TEX_DESC, buf, off)))
        off += struct.calcsize(TEX_DESC)
        blob = buf[off:off + d["data_bytes"]]
        off += d["data_bytes"]
        yield d, blob


# ---- minimal BCn block decoders (return RGBA8 rows) ----
def _rgb565(c):
    r = (c >> 11) & 0x1F
    g = (c >> 5) & 0x3F
    b = c & 0x1F
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def _bc_color_block(block, out, ox, oy, w, h, opaque_punch):
    c0, c1 = struct.unpack_from("<HH", block, 0)
    bits = struct.unpack_from("<I", block, 4)[0]
    e0, e1 = _rgb565(c0), _rgb565(c1)
    pal = [e0, e1]
    if c0 > c1 or not opaque_punch:
        pal.append(tuple((2 * e0[i] + e1[i]) // 3 for i in range(3)))
        pal.append(tuple((e0[i] + 2 * e1[i]) // 3 for i in range(3)))
        a3 = 255
    else:
        pal.append(tuple((e0[i] + e1[i]) // 2 for i in range(3)))
        pal.append((0, 0, 0))
        a3 = 0
    for py in range(4):
        for px in range(4):
            idx = (bits >> (2 * (4 * py + px))) & 3
            x, y = ox + px, oy + py
            if x < w and y < h:
                r, g, b = pal[idx]
                a = a3 if idx == 3 and a3 == 0 else 255
                p = (y * w + x) * 4
                out[p:p + 4] = bytes((r, g, b, a))


def _bc2_alpha(block, out, ox, oy, w, h):
    a = struct.unpack_from("<Q", block, 0)[0]
    for py in range(4):
        for px in range(4):
            x, y = ox + px, oy + py
            if x < w and y < h:
                av = (a >> (4 * (4 * py + px))) & 0xF
                out[(y * w + x) * 4 + 3] = av * 17


def _bc3_alpha(block, out, ox, oy, w, h):
    a0, a1 = block[0], block[1]
    abits = int.from_bytes(block[2:8], "little")
    if a0 > a1:
        al = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    else:
        al = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
    for py in range(4):
        for px in range(4):
            x, y = ox + px, oy + py
            if x < w and y < h:
                ai = (abits >> (3 * (4 * py + px))) & 7
                out[(y * w + x) * 4 + 3] = al[ai]


def decode_bc(blob, w, h, fmt):
    out = bytearray(w * h * 4)
    bw, bh = (w + 3) // 4, (h + 3) // 4
    block_bytes = 8 if fmt == 1 else 16
    off = 0
    for by in range(bh):
        for bx in range(bw):
            blk = blob[off:off + block_bytes]
            off += block_bytes
            if len(blk) < block_bytes:
                return out
            if fmt == 1:  # BC1
                _bc_color_block(blk, out, bx * 4, by * 4, w, h, True)
            elif fmt == 2:  # BC2
                _bc_color_block(blk[8:], out, bx * 4, by * 4, w, h, False)
                _bc2_alpha(blk, out, bx * 4, by * 4, w, h)
            else:  # BC3
                _bc_color_block(blk[8:], out, bx * 4, by * 4, w, h, False)
                _bc3_alpha(blk, out, bx * 4, by * 4, w, h)
    return out


def write_png(path, w, h, rgba):
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgba[y * w * 4:(y + 1) * w * 4]
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def decode_textures(buf, h, hdr_size, base):
    for i, (d, blob) in enumerate(iter_textures(buf, h, hdr_size)):
        w, hh, fmt = d["width"], d["height"], d["tex_format"]
        if fmt == 0:  # RGBA8 already (tight rows)
            rgba = bytearray(blob[:w * hh * 4])
            rgba += bytes(w * hh * 4 - len(rgba))
        else:
            rgba = decode_bc(blob, w, hh, fmt)
        out = f"{base}.tex{i}.{TEXFMT.get(fmt,'?')}.{w}x{hh}.png"
        write_png(out, w, hh, rgba)
        print(f"    tex{i}: {TEXFMT.get(fmt,'?')} {w}x{hh} swz=0x{d['swizzle']:03X} -> {out}")


def process_file(path, verbose, png):
    with open(path, "rb") as f:
        buf = f.read()
    h, hdr_size, version = parse_header(buf)
    if h is None:
        if version is None:
            print(f"{os.path.basename(path)}: not a valid H3DP packet")
        else:
            print(f"{os.path.basename(path)}: unsupported packet v{version} "
                  f"(this tool decodes v3/v4 — re-dump with the current build)")
        return None
    print(f"{os.path.basename(path):26} {fmt_draw_line(h)}")
    if png:
        decode_textures(buf, h, hdr_size, os.path.splitext(path)[0])
    return h


def main():
    ap = argparse.ArgumentParser(description="Decode high-cut path-C draw packets.")
    ap.add_argument("target", help="a .bin packet, or a build dir containing highcut_frame.count")
    ap.add_argument("--verbose", action="store_true", help="(reserved) per-draw detail")
    ap.add_argument("--png", action="store_true", help="decode each draw's textures to PNG")
    args = ap.parse_args()

    if os.path.isdir(args.target):
        cnt_path = os.path.join(args.target, "highcut_frame.count")
        if not os.path.exists(cnt_path):
            print(f"no highcut_frame.count in {args.target}")
            return 1
        with open(cnt_path) as f:
            count = int(f.read().split()[0])
        print(f"frame: {count} captured owned draws")
        stencil_draws, depth_draws, indexed = [], [], 0
        is3d = []  # per-draw: main-3D content (vp_w==640 & textured), for frame-window finding
        for i in range(count):
            p = os.path.join(args.target, f"highcut_frame_{i}.bin")
            if not os.path.exists(p):
                is3d.append(False)
                continue
            h = process_file(p, args.verbose, args.png)
            if h and h.get("stencil_enable"):
                stencil_draws.append(i)
            if h and h.get("depth_enable"):
                depth_draws.append(i)
            if h and h.get("index_format"):
                indexed += 1
            is3d.append(bool(h and h.get("vp_w") == 640.0 and h.get("texture_count", 0) > 0))
        # Largest contiguous run of main-3D-content draws — a good single-frame replay window. (The
        # live-3D capture accumulates several frames since IssueSwap can't delimit them; replay this
        # window to view one frame without the full-screen mask draws that blackout the RT.)
        best = (0, 0, 0); cur = 0; start = 0
        for i, ok in enumerate(is3d):
            if ok:
                if cur == 0:
                    start = i
                cur += 1
                if cur > best[0]:
                    best = (cur, start, i)
            else:
                cur = 0
        print(f"\nsummary: {len(depth_draws)} depth-tested, {indexed} indexed (kGuestDMA), "
              f"{len(stencil_draws)} stencil-tested draws")
        print(f"         stencil-tested: {stencil_draws}")
        if best[0]:
            print(f"\nlargest 3D-content run: {best[0]} draws [{best[1]}..{best[2]}] — replay one frame:")
            print(f'  $env:NHL_HIGHCUT_C5_MINDRAW="{best[1]}"; '
                  f'$env:NHL_HIGHCUT_C5_MAXDRAW="{best[2] + 1}"; .\\_c5render.ps1')
        return 0
    process_file(args.target, args.verbose, args.png)
    return 0


if __name__ == "__main__":
    sys.exit(main())
