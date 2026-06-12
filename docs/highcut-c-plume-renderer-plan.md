# High-cut path C — own the renderer on plume (CP-decode → plume RHI)

> **Goal:** render the game on a renderer **we own** (plume RHI, Vulkan in-process), at flat
> logical sizes, with **no EDRAM / no fold / no SDK `final` ceilings** — the high enhancement +
> performance ceiling chosen over the flat-RT hybrid (see [[high-cut-pivot-decision]],
> docs ranking in the session that picked C).

## What C is (and isn't)

C is **not** a pure D3D9 reimplementation — H-1 proved the game's **draws and RT-binds are
inlined** in the recompiled code, so they can't be hooked as clean D3D9 calls. So C =

```
recompiled game → PM4 → [SDK CommandProcessor decode]  → OUR plume renderer (flat, no EDRAM)
                         reused FRONT-END (decode only)   NEW BACK-END (we own RTs/passes/PSOs)
```

We **reuse the SDK's decode front-end** (PM4 parse, PrimitiveProcessor index/vertex, register
decode for viewport/constants/fetch-constants/RT+texture bindings, and the **SPIR-V shader
translator**) and **replace the output** — instead of submitting to the SDK D3D12 backend
(EDRAM, `final` RT cache, upload ring, the fold), we build plume pipelines/buffers/textures and
render into **flat host RTs at logical size**.

## Architecture decisions (validated this session)

1. **Backend = plume Vulkan.** A 2nd D3D12 device in-process TDRs rexglue ([[highcut-h2-plume-present]]);
   plume Vulkan coexists. (plume D3D12 only becomes viable once rexglue's GPU is fully off — a
   late "takeover" milestone, deferred.)
2. ~~**Shaders = SPIR-V via the SDK.**~~ **DEAD (2026-06-11):** the SDK's `win-amd64` build is
   **D3D12-only** — it ships the `spirv_translator.h` *header* but the implementation is NOT in
   `rexruntime.lib` (`llvm-nm`: **0 `SpirvShaderTranslator` symbols**), and the header transitively
   needs glslang's `SPIRV/SpvBuilder.h` which isn't shipped either. So Xenos→SPIR-V via the SDK is
   not linkable. **REVISED — shaders = DXBC, backend = plume D3D12 sharing rexglue's device:**
   - Use the SDK's **DXBC** translator (already working in the beta path, exported) → DXBC bytes.
   - plume's D3D12 `createShader` only `assert`s the format *label* == DXIL but copies the raw
     bytecode into the PSO; the **D3D12 runtime accepts DXBC** containers → label DXBC as `DXIL`
     and it works. **No SPIR-V, no DXIL conversion, no glslang.**
   - The 2nd-D3D12-device TDR ([[highcut-h2-plume-present]]) is avoided by **patching vendored
     plume `D3D12Device` to ADOPT rexglue's existing `ID3D12Device`** (one device, multiple queues
     — standard D3D12) instead of creating its own. plume is vendored source, so this is fair game.
   - **RISK (must validate first):** sharing rexglue's *live, actively-submitting* device is a
     hypothesis (only the 2nd-*device* TDR was proven; one-device-multi-queue should be fine but
     isn't yet shown here). C's revised first step is a triangle on the adopted device.
   - Consequence: C is all-**D3D12** (present too can move off the H-2 Vulkan path once the device
     is shared), and the H-2 plume-Vulkan present becomes a fallback, not the C render path.
3. **Resources are ours.** Read guest RAM directly, upload to plume buffers (vertex/index/constant)
   — no shared-memory/write-watch/upload-ring machinery. Textures need an **untile** (Xenos tiled →
   linear) into a plume texture; do CPU untile first (simple), GPU-compute untile later (fast).
4. **RTs are flat.** Each guest color/depth surface → a plume RT at **logical** size (1280×720 /
   actual), never EDRAM pitch. Guest Resolve → host copy plume-RT → plume-texture. The fold cannot
   occur because EDRAM never exists here.
5. **Present = plume swapchain** (H-2 already drives it per guest Present).

## Shader path DECISION (2026-06-11, user): port a Xenos→SPIR-V translator (keep plume Vulkan)

Chosen over the shared-device D3D12+DXBC path to keep the render path fully decoupled from
rexglue's live device (no shared-device risk). What the port concretely requires:

- The SDK's `spirv_translator.h` IS Xenia's `spirv_shader_translator.h` (© Ben Vanik 2022, adapted
  to `rex::` by Tom Clay 2026), but the **implementation is not shipped**. So vendor the matching
  **Xenia `spirv_shader_translator*.cc`** (Xenia splits it across ~6–8 files: base + `_alu` +
  `_fetch` + `_rb` + `_memexport` + …), adapted to the `rex::` namespace and the SDK's
  `Shader`/`ShaderTranslator`/`Translation` types.
- **Dependency: glslang** — Xenia's translator emits via glslang's `SPIRV/SpvBuilder.h`. Vendor
  glslang (FetchContent) + its sub-deps (SPIRV-Tools, SPIRV-Headers) for the optimize/validate pass.
- **Risk: header drift.** Must find the Xenia commit whose `spirv_shader_translator.h` matches the
  SDK header, take its `.cc`, and reconcile any `rex::`-adaptation differences. The base
  `ShaderTranslator::TranslateAnalyzedShader` IS exported (so the analyzed-Shader plumbing is
  reusable); only the SPIR-V subclass impl is missing.
- **Effort: large / multi-session**, and it requires authorizing the vendoring of two substantial
  external codebases (glslang + Xenia translator source). The shared-device D3D12 alternative was a
  ~one-function plume patch reusing the already-working DXBC translator; kept as a fallback if the
  port's cost/risk proves prohibitive (the device-share hypothesis is cheaply testable in isolation).

Port milestones: **P-1 DONE (2026-06-11)** — glslang 14.3.0 vendored (FetchContent, ENABLE_OPT/HLSL
off), builds+links in the clang/MSVC toolchain, `spv::Builder` emits valid SPIR-V (magic 0x07230203),
and the SDK's `spirv_builder.h` compiles against it with no fatal API drift (probe:
`gpu/hooks/highcut_spirv_probe.cpp`, gate `NHL_HIGHCUT_SPIRV_PROBE`). Original P-1 plan:
**P-2** vendor+adapt the Xenia SPIR-V translator `.cc`, link against the SDK header, translate one
analyzed `beta_current_vs_` → valid SPIR-V (byte-validate with spirv-val); **P-3** feed that SPIR-V
to plume-Vulkan `createShader` + build a pipeline; then rejoin the C milestones below at C-3.

## Milestones (incremental, each independently testable)

- **C-1 — plume renders GEOMETRY (not just clears).** Extend the in-process plume thread
  (`gpu/hooks/plume_present.cpp`) to draw a triangle: SPIR-V VS/PS + vertex buffer + pipeline +
  `drawInstanced`, into the swapchain. Template = `third_party/plume/examples/triangle/main.cpp`.
  Proves the plume geometry + SPIR-V + pipeline path works in the required Vulkan/in-process setup.
  *Done = a triangle in the plume window, synced to guest Present.* **(START HERE.)**
- **C-2 — Xenos ucode → SPIR-V → plume.** Drive `SpirvShaderTranslator` on one real guest shader
  and render a triangle with the translated SPIR-V (hardcoded geometry). Proves the shader bridge.
- **C-3 — one real decoded guest draw.** Bridge from the CP decode: take a 2D menu draw's
  vertex/index/constants/viewport (the data the beta CP already decodes in `RenderBetaOwnedDraw`),
  upload to plume, build a pipeline from its translated SPIR-V, draw it flat. Solid-color first
  (defer textures). *Done = a recognizable menu element rendered by plume.*
- **C-4 — textures.** Untile guest tiled textures → plume textures; samplers; bind. *Done = a
  textured menu draw.*
- **C-5 — full frame, flat multi-pass.** All draws of a frame; per-surface flat plume RTs; guest
  Resolve = host copy. Validate menu, then a 3D scene (the fold is structurally absent → no shear).
- **C-6 — takeover.** Suppress rexglue's present/GPU where possible so plume is the only output;
  optionally switch plume to D3D12 once rexglue's device is off.

## Where the reusable decode lives

`renderer/core/nhl_command_processor.cpp::RenderBetaOwnedDraw` already decodes every piece C-3+
needs (ProcessingResult, vfetch constants → addr/size, texture fetch constants, float/bool
constants, viewport/NDC, primitive topology, index buffer). C reuses that decode and swaps the
**output half** (SDK deferred command list → plume command list). The live residency learning
([[beta-live-render-path]]) carries over: read CURRENT guest RAM per draw (force-refresh dynamic
ranges) — but C uploads to its own plume buffers, sidestepping the SDK ring entirely.

## Risks / unknowns

- **SpirvShaderTranslator driving.** Need the exact API to translate a `Shader` ucode → SPIR-V
  outside the SDK's Vulkan pipeline cache (modification/register-count setup). C-2 derisks this.
- **Vertex-fetch in SPIR-V.** Xenia's SPIR-V VS pulls vertices via the shared-memory SSBO model;
  on plume we may instead supply a real IA vertex buffer + input layout (simpler) OR replicate the
  SSBO fetch. Decide at C-3 (start with IA vertex buffers from decoded vfetch ranges).
- **Untile.** CPU untile is simple but slow; fine for bring-up, optimize at C-4/C-5.
- **Coexistence cost.** Until C-6, both rexglue's GPU and plume run — extra overhead, acceptable
  for bring-up.

## Test harness

In-process, env-gated `NHL_HIGHCUT_PRESENT` (Vulkan plume thread). The game boots normally;
plume renders in its own window driven by guest Present. No takeover of the game's output until
C-6, so every milestone is additive and safe to leave gated-off.
