# High-cut C, C-5d — per-surface depth/stencil + frame delimiting (kickoff prompt)

> **Self-contained kickoff for a fresh session.** Mission: make a **full 3D gameplay frame** composite
> correctly on the plume replay. C-5c proved the geometry pipeline (a real rink renders with correct
> depth occlusion, back-face cull, textures, and **indexed** draws — no fold, no explosion, 0 VUID),
> but the FULL frame doesn't compose because (1) all draws share ONE flat depth+stencil buffer, so a
> frame-start mask draw both pollutes the 3D depth AND is the only source of the stencil the ice/crowd
> draws test against — you can't have it both ways on one buffer; and (2) a live-3D capture stacks
> SEVERAL frames because the takeover path never calls IssueSwap, so there's no frame boundary. C-5d
> fixes both. Full path-C plan + history: `docs/highcut-c-plume-renderer-plan.md`. Memory:
> `[[highcut-c-plume-renderer]]`.

## Where this sits (context — DO NOT re-derive)

High-cut **path C** renders the game on a renderer we own (plume RHI, **Vulkan**, flat RTs, **no
EDRAM/fold**): `recompiled game → PM4 → SDK CommandProcessor DECODE → OUR plume renderer`. A frame is
captured to `highcut_frame_<N>.bin` (one self-describing packet per owned draw) and the plume thread
replays them all into the swapchain.

**Proven + committed through C-5c (DO NOT redo):**
- **Menu (C-5a/b):** ~90% faithful 2D menu renders.
- **C-5c (DONE):** depth+stencil buffer + per-draw depth/stencil/cull (packet **v4**); a real 3D rink
  renders with correct occlusion, cull, textures, no fold/shear, 0 VUID.
- **C-5c indexed-draw fix (packet v5):** the 3D "explosion" (triangles radiating from a point) was
  **unhandled indexed drawing** — 1266/1287 of a gameplay frame's draws are `index_buffer_type ==
  kGuestDMA` (indexed), but the replay only did non-indexed `drawInstanced`, so vertices drew in
  storage order. FIX: capture the raw guest index bytes (`IndexBufferInfo{format,count,guest_base,
  length}`; big-endian — the VS swaps `gl_VertexIndex` via `vertex_index_endian`, mirroring the beta
  `kGuestDMA` path) appended at the END of the packet; replay does `drawIndexedInstanced` with an
  R16/R32_UINT index buffer. `RenderableDraw.indexU32` picks the element size. **Also fixed this pass:**
  multi-binding vertex capture (pack ALL `vertex_bindings()`, rebase each fetch-constant to its packed
  dword offset — 3D meshes stream position/normal/uv separately) and per-draw SSBO sizing (was a fixed
  64KB, truncated 100–240KB meshes).
- **Capture-finalize is IssueSwap-independent:** the count + per-frame index reset live in
  `RenderBetaOwnedDraw` (reset on `frame_index_` change, rewrite `highcut_frame.count` after every
  dumped draw) — because live-3D takeover never reaches IssueSwap before the process is killed.

**Offline decode tool (USE IT):** `python tools/highcut_packet_decode.py <build_dir>` parses v3/v4/v5
packets, prints per-draw depth/stencil/cull/index state, counts depth/indexed/stencil draws, and
**prints the exact `NHL_HIGHCUT_C5_MINDRAW/MAXDRAW` window for the largest contiguous 3D run** (the
current way to view one frame's main geometry). `--png` decodes BCn/RGBA8 textures to PNG.

## The two problems C-5d must solve

### Problem 1 — per-surface depth/stencil (the "missing foreground")
The captured frame has a **frame-start mask draw** (e.g. draw 0: `vp=320×180`, full-screen quad,
`depth=ENW/Always`, `stencil_enable`, `pass_op=Replace`, `wmask=0xFF`, `cwm=0xF`). On real hardware it
targets its OWN surface and its job is to **set stencil**; later ice/crowd draws (large `vp=640`,
`tex=0`, `stencil_enable`, `cwm=0xF`) **test that stencil**. On the flat C-5 path ALL draws share ONE
depth+stencil buffer, so:
- **Include the mask** → its `depth=Always` write stamps mid/near depth across the screen → the 3D
  geometry (`depth=LEqual`) fails the depth test → black (proven: `NHL_HIGHCUT_C5_MINDRAW=1` to skip
  draw 0 unblocked the boards).
- **Skip the mask** → the stencil it would have written is absent → the ice/crowd `stencil`-tested
  draws fail the stencil test → invisible → only the far boards render as an isolated band (the
  "vertical squash" the user saw is really missing stencil-gated foreground).

So one shared buffer can't satisfy both. **The fix is per-surface render targets:** group draws by
their guest render surface (`RB_SURFACE_INFO` base/pitch + `RB_DEPTH_INFO` base, + viewport), give each
guest color/depth surface its own flat plume RT at logical size, and replay each draw into ITS surface's
RT (its own depth+stencil). Then resolve/composite per the guest's **Resolve** (render-to-texture copy):
`sub_827EF8E0` is the Resolve (count-exact, see `[[highcut-h1-resource-graph]]`); a guest Resolve =
host copy of the source RT region into a plume texture keyed by the dest guest address, so a later draw
that samples that address gets the resolved image. This is the originally-planned C-5d increment
(per-surface flat RTs + Resolve=host-copy → correct render-to-texture composition: shadow maps,
reflections, the rink-cam, post). Capture must tag each packet with its surface identity (color base,
depth base, surface pitch, msaa) so the replay can bucket draws into RTs.

### Problem 2 — frame delimiting (captures stack multiple frames)
Live-3D takeover **never calls `NhlD3D12CommandProcessor::IssueSwap`** (verified: 0 swap-log lines
after takeover arms; `frame_index_` frozen; the guest presents via `PresentBetaLiveFrame`, not the swap
that bumps `frame_index_`). So the per-frame reset (keyed on `frame_index_`) never fires and a capture
accumulates SEVERAL frames (saw 1287–2888 draws = 2–3 frames). The replay stacks them and ends on the
last frame's full-screen mask draws (`cwm=0xF`, One/Zero) that overwrite the RT → **black**. WORKAROUND
today: replay the decoder's largest-3D-run window. **C-5d needs a real frame boundary from the
DRAW STREAM, independent of IssueSwap.** Candidates (pick after a short spike):
- The **guest present hook** (`sub_827F1C88`, hooked in `gpu/hooks/d3d9_resources.cpp`, bumps
  `g_guestFrames` for plume) fires per guest frame regardless of takeover — expose its count to the CP
  (extern/global) and reset `highcut_capture_idx_` when it advances. Most robust.
- OR detect the frame-start in the stream (the first draw to the primary surface after a Resolve/clear,
  or the recurring frame-start mask draw). Heuristic; verify against the present-hook count.
Once frames are delimited, a single 3D frame captures cleanly (no window juggling).

## Suggested order
1. **Frame delimiting first** (Problem 2) — small, unblocks clean single-frame captures so everything
   downstream is easier to judge. Spike the guest-present-hook counter → CP reset.
2. **Per-surface RTs** (Problem 1) — capture surface identity per draw; bucket draws into per-surface
   plume RTs (each its own depth+stencil); replay per surface.
3. **Resolve = host copy** — implement guest Resolve as a host copy into an address-keyed plume texture;
   wire sampling draws to it. Validate the full rink frame composes (ice + boards + crowd + goal).

## Gotchas
1. **The mask draw's depth vs stencil conflict is the whole point** — don't "fix" it by globally
   disabling the mask's depth write; that breaks frames where the mask legitimately seeds depth. Solve
   it with per-surface separation.
2. **MSAA** — 3D surfaces may be 2X/4X (frame-feature inventory). Per-surface RTs can be multisampled
   with a resolve to 1X; or render 1X first and add MSAA after. Don't crash pipeline creation on an
   MSAA mismatch.
3. **Resolve source vs dest pitch** — flat path has no EDRAM fold ([[tiling-verdict-no-game-tiling]]),
   so a Resolve is a straight region copy at logical size; don't reintroduce pitch math.
4. **Per-draw viewport** — C-5c renders every draw at the FULL swapchain viewport; 3D draws use
   `vp=(0,0,640,360)` with **identity `ndc_scale=(1,-1,1)`** (geometry already in NDC), so the full
   viewport is correct for the main scene. But per-surface RTs at different sizes (320×180 mask,
   384-wide shadow passes) will need their own viewports — apply the packet's `vp_*` per surface.
5. **Validation layer** — keep targeting 0 VUID (`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` +
   `vk_layer_settings.txt`). Multiple RTs + barriers per surface is a new VUID surface.

## Build / run / verify
- Build: `_build_beta.bat` → `BUILD_EXIT=0`.
- Capture: `_c5dump.ps1` (beta-live + F10 at a 3D scene, hold). After C-5d frame-delimiting,
  `highcut_frame.count` should be ONE frame's draws (not thousands).
- Inspect: `python tools/highcut_packet_decode.py <build_dir>` (counts + 3D window + `--png` textures).
- Replay: `_c5render.ps1` (`NHL_HIGHCUT_PRESENT=1 NHL_HIGHCUT_C5=1` + validation layer). Bisect with
  `NHL_HIGHCUT_C5_MINDRAW/MAXDRAW`. Existing 3D gates: `NHL_HIGHCUT_NOCULL`, `NHL_HIGHCUT_FLIP_FACE`,
  `NHL_HIGHCUT_NO_YFLIP`.

## Done criteria
- A single 3D gameplay frame **captures as one frame** (frame-delimited) and **composites correctly**
  on plume: ice + boards + goal + crowd all present, correct depth occlusion, stencil-gated content
  visible, render-to-texture passes resolved — validation-clean.
- Menu unregressed. Commit C-5d; update `docs/highcut-c-plume-renderer-plan.md` + `[[highcut-c-plume-renderer]]`.

## Key files + pointers
- Packet: `gpu/hooks/highcut_draw_packet.h` (v5; bump to v6 when adding surface identity).
- Capture: `renderer/core/nhl_command_processor.cpp` `RenderBetaOwnedDraw` — vertex/index/state capture
  (~1885 vertex+index blobs, ~2089 header), per-frame reset + count (`frame_index_` change). Surface
  regs: `RB_SURFACE_INFO` (pitch), `RB_COLOR_INFO`/`RB_DEPTH_INFO` (bases), the ROV/Resolve decode.
  Resolve = `sub_827EF8E0`.
- Replay: `gpu/hooks/plume_present.cpp` (`RenderableDraw`, `BuildRenderableDraw`, `LoadC5Frames`,
  `RenderClear`/render loop, `CreateFramebuffers` — currently ONE shared depth `c.depthTex`; C-5d makes
  per-surface RTs). plume types in `third_party/plume/plume_render_interface_types.h`.
- Present hook (frame signal): `gpu/hooks/d3d9_resources.cpp` (`sub_827F1C88` → `g_guestFrames`).
- Tool: `tools/highcut_packet_decode.py`. Scripts: `_c5dump.ps1` / `_c5render.ps1`. Memory:
  `[[highcut-c-plume-renderer]]`.
