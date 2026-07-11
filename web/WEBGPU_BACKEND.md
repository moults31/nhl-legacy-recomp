# WebGPU Rendering Backend — Design for WASM Port

## 1. Scope & Goal

Boot-to-menu POC. The menu is 2D UI/compositing driven by CPU-side guest code
(XMA audio, title screen rendering via resolve-copy, ImGui overlay). The FSI
path (fragment-shader-interlock per-tile EDRAM resolves) is NOT needed.

**Key constraint**: Emscripten 3.1.74 has **no** Vulkan shim.
`webgpu/webgpu.h` (WebGPU-native C header) is the only GPU API.

## 2. WebGPU API Surface

The WebGPU C header (`webgpu/webgpu.h`, from `webgpu-headers` repository) exposes:

| Category | Key Entry Points |
|---|---|
| **Instance** | `wgpuCreateInstance(nullptr)` → `WGPUInstance` |
| **Adapter** | `wgpuInstanceRequestAdapter(instance, options, callback, userdata)` → `WGPUAdapter` |
| **Device** | `wgpuAdapterRequestDevice(adapter, descriptor, callback, userdata)` → `WGPUDevice` |
| **Surface** | `wgpuInstanceCreateSurface(instance, descriptor)` → `WGPUSurface` |
| **Swapchain** | `wgpuSurfaceGetCurrentTexture(surface, &texture)` → `WGPUSurfaceTexture` |
| | `wgpuSurfaceConfigure(surface, &config)` |
| **Buffers** | `wgpuDeviceCreateBuffer(device, &descriptor)` → `WGPUBuffer` |
| **Textures** | `wgpuDeviceCreateTexture(device, &descriptor)` → `WGPUTexture` |
| **Texture Views** | `wgpuTextureCreateView(texture, &descriptor)` → `WGPUTextureView` |
| **Shaders** | `wgpuDeviceCreateShaderModule(device, &descriptor)` → `WGPUShaderModule` (WGSL only) |
| **Pipelines** | `wgpuDeviceCreateRenderPipeline(device, &descriptor)` → `WGPURenderPipeline` |
| **Command Encoding** | `wgpuDeviceCreateCommandEncoder(device, &descriptor)` → `WGPUCommandEncoder` |
| | `wgpuCommandEncoderBeginRenderPass(encoder, &descriptor)` → `WGPURenderPassEncoder` |
| | `wgpuRenderPassEncoderSetPipeline(pass, pipeline)` |
| | `wgpuRenderPassEncoderDraw(pass, vertexCount, instanceCount, ...)` |
| | `wgpuCommandEncoderFinish(encoder)` → `WGPUCommandBuffer` |
| **Queue** | `wgpuDeviceGetQueue(device)` → `WGPUQueue` |
| | `wgpuQueueSubmit(queue, count, commands)` |
| **Emscripten** | `WGPUEmscriptenSurfaceSourceCanvasHTMLSelector` — surface from `#canvas` selector |

The header is **C-only** (opaque handles, `WGPU` prefix). There is also a C++ wrapper
(`webgpu/webgpu_cpp.h`) with `wgpu::` namespace RAII types.

## 3. SDK Architecture (Vulkan Backend Reference)

### 3.1 Class Hierarchy

```
IGraphicsSystem (rex::system)         — pure abstract: SetupPresentation, SetupGuestGpu, Shutdown
  └── GraphicsSystem (rex::graphics)  — shared logic: register file, vsync, interrupt dispatch
        └── VulkanGraphicsSystem      — creates VulkanProvider + VulkanCommandProcessor
              └── NhlVkGraphicsSystem — game-specific subclass (overrides CreateCommandProcessor)

CommandProcessor (rex::graphics)      — abstract GPU command processor
  └── VulkanCommandProcessor          — 6924 LOC: full PM4 execution, draw/copy dispatch, EDRAM
        └── NhlVkCommandProcessor     — game-specific hook (IssueSwap/IssueDraw perf counting)

GraphicsProvider (rex::ui)            — factory for Presenter + ImmediateDrawer
  └── VulkanProvider                  — owns VulkanInstance + VulkanDevice + UISamplers

Presenter (rex::ui)                   — guest output display + ImGui overlay
  └── VulkanPresenter                 — VkSwapchainKHR, FSR/CAS scaling, letterboxing
```

### 3.2 Key Virtual Methods to Implement

#### GraphicsSystem (abstract base, 2 pure virtuals)

```cpp
virtual void CreateProvider(bool with_presentation) = 0;
virtual std::unique_ptr<CommandProcessor> CreateCommandProcessor() = 0;
```

Plus inherited `IGraphicsSystem` interface:
```cpp
virtual X_STATUS SetupPresentation(WindowedAppContext* app_context);
virtual X_STATUS SetupGuestGpu(FunctionDispatcher*, KernelState*);
virtual void Shutdown();
```

#### CommandProcessor (abstract base, 5+ pure virtuals)

```cpp
virtual bool SetupContext() = 0;           // Create GPU device/context
virtual void ShutdownContext() = 0;        // Destroy GPU device/context

virtual Shader* LoadShader(...) = 0;       // Compile guest ucode → host shader
virtual bool IssueDraw(...) = 0;           // Execute indexed draw call
virtual bool IssueCopy() = 0;              // Execute resolve-copy (EDRAM → RAM)
virtual void IssueSwap(...) = 0;           // Present front buffer
virtual void TracePlaybackWroteMemory(...) = 0;
virtual void RestoreEdramSnapshot(...) = 0;
```

#### GraphicsProvider (abstract base, 2 pure virtuals)

```cpp
virtual std::unique_ptr<Presenter> CreatePresenter(...) = 0;
virtual std::unique_ptr<ImmediateDrawer> CreateImmediateDrawer() = 0;
```

#### Presenter (abstract base, 4+ pure virtuals)

```cpp
virtual bool CaptureGuestOutput(RawImage& image_out) = 0;
virtual SurfacePaintConnectResult ConnectOrReconnectPaintingToSurfaceFromUIThread(...) = 0;
virtual void DisconnectPaintingFromSurfaceFromUIThreadImpl() = 0;
virtual bool RefreshGuestOutputImpl(uint32_t mailbox_index, ...) = 0;
virtual PaintResult PaintAndPresentImpl(bool execute_ui_drawers) = 0;
```

### 3.3 Vulkan Backend File Breakdown (~21,700 LOC total)

| File | LOC | Purpose |
|---|---|---|
| `graphics_system.cpp` | 35 | Creates provider + command processor (trivial) |
| `command_processor.cpp` | 6,924 | PM4 stream interpreter, IssueDraw, IssueCopy, IssueSwap, all Xenos register callbacks |
| `render_target_cache.cpp` | 6,359 | EDRAM emulation: FSI/FBO paths, resolve/copy shaders, depth store |
| `texture_cache.cpp` | 3,495 | Guest texture → host GPU texture mapping |
| `pipeline_cache.cpp` | 3,702 | SPIR-V pipeline compilation, descriptor set layouts, render passes |
| `shared_memory.cpp` | 447 | GPU-side memory write tracking (memexport emulation) |
| `primitive_processor.cpp` | 234 | Xenos primitive topology → Vulkan topology |
| `deferred_command_buffer.cpp` | 438 | Staging buffer for deferred GPU writes |
| `shader.cpp` | 72 | Shader object wrapper |

For **menu-only POC**, only `graphics_system.cpp` and `command_processor.cpp` need real
implementations. Everything else can be stubbed.

## 4. Minimal WebGPU Backend Design

### 4.1 What to Implement (Menu-Critical)

| Component | Status | Notes |
|---|---|---|
| **WebGpuGraphicsSystem** | Implement | Trivial (~40 LOC, mirrors VulkanGraphicsSystem) |
| **WebGpuProvider** | Implement | Owns WGPUInstance, WGPUAdapter, WGPUDevice |
| **WebGpuPresenter** | Implement | Swapchain from canvas, simple blit for guest output, ImGui overlay |
| **WebGpuCommandProcessor** | Implement | IssueSwap (present framebuffer), IssueDraw (stub), IssueCopy (stub) |
| **Canvas surface setup** | Implement | `#canvas` element → `WGPUSurface` via `EmscriptenSurfaceSourceCanvasHTMLSelector` |

### 4.2 What to Stub / No-op

| Component | Status | Rationale |
|---|---|---|
| **RenderTargetCache** | Stub | No FSI/FBO EDRAM emulation needed for menu |
| **TextureCache** | Stub | Menu textures are CPU-composited into front buffer |
| **PipelineCache** | Stub | No guest draw calls in menu (PM4 SET_SHADER/DRAW_INDX are no-ops) |
| **SharedMemory** | Stub | No GPU-side memory writes from menu shaders |
| **PrimitiveProcessor** | Stub | No geometry processing |
| **LoadShader** | Return nullptr | Guest shaders are Xenos ucode → SPIR-V. Menu doesn't use them |
| **IssueDraw** | Return false | Menu doesn't issue 3D draws |
| **IssueCopy** | Return false | No EDRAM resolve needed for menu |
| **RestoreEdramSnapshot** | No-op | No EDRAM state to restore |
| **TracePlaybackWroteMemory** | No-op | No tracing in WASM |

### 4.3 WebGPU Workflow for Menu

```
Initialize:
  1. wgpuCreateInstance(nullptr) → WGPUInstance
  2. wgpuInstanceRequestAdapter(instance, ...) → WGPUAdapter
  3. wgpuAdapterRequestDevice(adapter, ...) → WGPUDevice
  4. wgpuInstanceCreateSurface(surface, canvasSel) → WGPUSurface
  5. wgpuSurfaceConfigure(surface, &config) → swapchain

Per Frame (IssueSwap):
  1. wgpuSurfaceGetCurrentTexture(surface, &tex) → swapchain texture
  2. Create command encoder
  3. Copy guest frontbuffer (CPU buffer) → swapchain texture
  4. (Future) Render ImGui overlay
  5. Submit to queue
  6. surface.Present() (in C++, wgpuSurfacePresent)
```

### 4.4 SPIR-V → WGSL Mapping

The SDK's render pipeline compiles Xenos ucode → GLSL (via ucode disassembly) →
SPIR-V (via glslang). WebGPU accepts **only WGSL**, not SPIR-V.

**Options for menu-only:**

| Approach | Effort | Notes |
|---|---|---|
| **Hardcoded WGSL** | Low | Write ~5 simple WGSL shaders for 2D compositing, blit, ImGui |
| **SPIRV-Cross** | Medium | Offline: SPIR-V → WGSL at build time. The SDK's resolve-copy shaders are precompiled SPIR-V |
| **Tint (Google/Dawn)** | High | Google's SPIR-V → WGSL compiler, integrated in Dawn. Can compile in-browser via WASM |
| **naga (wgpu-rs)** | Medium | Rust crate, same domain. CSS/wasm compilation needed |

**Recommendation**: Hardcoded WGSL for menu POC. Menu compositing needs:
1. A fullscreen triangle/quad vertex shader
2. A fragment shader that samples the frontbuffer (CPU-side RGBA8) and writes to swapchain
3. A gamma-ramp apply shader (optional; can be done on CPU)
4. ImGui WebGPU backend shaders (provided by `imgui_impl_wgpu.cpp`)

For the eventual full 3D path, SPIRV-Cross cross-compiled to WASM is the pragmatic choice —
it's a single C++ library, ~20k LOC, compiles with Emscripten cleanly.

### 4.5 Canvas Surface Setup

```html
<canvas id="canvas" width="1280" height="720" style="width:100%;height:100%"></canvas>
```

```cpp
WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc = {};
canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
canvasDesc.selector = "#canvas";

WGPUSurfaceDescriptor surfDesc = {};
surfDesc.nextInChain = &canvasDesc.chain;
WGPUSurface surface = wgpuInstanceCreateSurface(instance, &surfDesc);
```

### 4.6 Browser Async Model

WebGPU is inherently asynchronous in browsers:
- `wgpuInstanceRequestAdapter` and `wgpuAdapterRequestDevice` use callbacks
- Unlike Dawn's native implementation, emscripten's WebGPU API maps to
  `navigator.gpu.requestAdapter()` which returns a Promise
- Emscripten handles this via its async callback system
- **Single-threaded WASM** (no `-pthread`): callbacks fire on the main thread
  via the JS event loop. `emscripten_set_main_loop_arg` drives the loop.

Recommended flow:
```
main()
  → wgpuCreateInstance
  → wgpuInstanceRequestAdapter(callback)
    → callback: wgpuAdapterRequestDevice(callback)
      → callback: CreateProvider + CreateCommandProcessor
        → emscripten_set_main_loop_arg(frame_callback)
```

## 5. File Plan

### New Files (in `src/wasm/`)

| File | LOC (est.) | Purpose |
|---|---|---|
| `webgpu_graphics_system.h` | ~30 | WebGpuGraphicsSystem class declaration |
| `webgpu_graphics_system.cpp` | ~60 | CreateProvider, CreateCommandProcessor |
| `webgpu_command_processor.h` | ~50 | WebGpuCommandProcessor class declaration |
| `webgpu_command_processor.cpp` | ~300 | IssueSwap (real), IssueDraw/IssueCopy/Copy (stubs) |
| `webgpu_provider.h` | ~40 | WebGpuProvider class declaration |
| `webgpu_provider.cpp` | ~200 | Instance/adapter/device creation, presenter/immediate drawer factory |
| `webgpu_presenter.h` | ~40 | WebGpuPresenter class declaration |
| `webgpu_presenter.cpp` | ~400 | Swapchain management, guest output paint, ImGui overlay |
| `nhl_webgpu_backend.h` | ~30 | NhlWebGpuGraphicsSystem (game-specific, mirrors nhl_vk_backend.h) |
| `nhl_webgpu_backend.cpp` | ~60 | CreateCommandProcessor override |
| **Total skeleton** | **~1,200** | |

### WebGPU SDK Port (Phase 4 Implementation)

| Component | LOC (est.) | From Vulkan LOC | Ratio |
|---|---|---|---|
| GraphicsSystem | ~60 | 35 | 1.7x (async init adds code) |
| Provider | ~200 | ~300 (VulkanProvider + instance + device) | 0.7x (simpler) |
| Presenter | ~400 | ~3,000+ (VulkanPresenter) | 0.13x (no FSR/CAS, no DXGI) |
| CommandProcessor | ~500 | 6,924 | 0.07x (stub most PM4 drawing) |
| **Total Phase 4** | **~1,200** | **~21,700** | **0.05x** |

### Files NOT Needed for Menu POC

All the heavy Vulkan files have NO WebGPU equivalent for menu:
- `render_target_cache.cpp` (6,359 LOC) → NOT NEEDED (no EDRAM)
- `texture_cache.cpp` (3,495 LOC) → NOT NEEDED (no guest textures)
- `pipeline_cache.cpp` (3,702 LOC) → NOT NEEDED (no guest shaders)
- `shared_memory.cpp` (447 LOC) → NOT NEEDED (no GPU writes)
- `primitive_processor.cpp` (234 LOC) → NOT NEEDED
- `deferred_command_buffer.cpp` (438 LOC) → NOT NEEDED

## 6. Key Design Decisions

1. **Async initialization**: WebGPU adapter/device creation is callback-driven.
   The GraphicsSystem::CreateProvider must use emscripten's async model. The
   windowed app context (`HTML5WindowedAppContext`) already uses
   `emscripten_set_main_loop_arg` for the run loop — init can chain callbacks
   before entering the main loop.

2. **No PM4 execution for menu**: The CommandProcessor's `ExecutePacketType3_*`
   methods for draw/copy can be no-ops. Only `IssueSwap` needs implementation
   to display the CPU-composited front buffer.

3. **Frontbuffer as CPU texture upload**: The guest writes the front buffer via
   CPU (memcpy into guest RAM). WebGpuPresenter reads this CPU buffer and uploads
   it as a texture for the swapchain blit. No GPU-side render target resolution.

4. **ImGui integration**: ImGui has an official WebGPU backend
   (`imgui/backends/imgui_impl_wgpu.cpp` + `imgui_impl_wgpu.h`). This handles
   font texture upload, draw data → GPU command encoding. The SDK's
   `ImGuiDrawer` needs a WebGPU variant — or better, the WebGpuPresenter can
   directly call `ImGui_ImplWGPU_RenderDrawData()`.

5. **Single-threaded only (Phase 1)**: No `-pthread`. All GPU work happens on
   the browser main thread via the emscripten main loop. This simplifies surface
   management (no `ProxyToMainThread` for canvas access).

6. **Emscripten 3.1.74 compatibility**: The Emscripten WebGPU implementation
   wraps the browser's `navigator.gpu` WebGPU API. The header
   `webgpu/webgpu.h` is the standard `webgpu-headers` repository C API. The
   C++ wrapper (`webgpu/webgpu_cpp.h`) provides RAII handle types.

## 7. Open Questions

| Question | Answer/Status |
|---|---|
| Where is `webgpu/webgpu.h` in emsdk 3.1.74? | Not found in filesystem — may require separate `webgpu-headers` or port install. The test file `webgpu_basic_rendering.cpp` uses `<webgpu/webgpu_cpp.h>` successfully at emscripten test time. |
| Does emscripten 3.1.74 support `WGPUSurface` from canvas? | Yes. The test file uses `EmscriptenSurfaceSourceCanvasHTMLSelector` with `surface.GetCurrentTexture()`. |
| Canvas size vs internal resolution? | The menu renders at 1280×720. Canvas can CSS-scale. Presenter handles letterboxing. |
| How are guest register writes handled without PM4 execution? | RegisterFile still processes MMIO writes. Draw/copy registers are ignored; frontbuffer address/format are read by IssueSwap. |
