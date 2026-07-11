# WASM Port — Implementation Plan

## Overview

NHL Legacy Recomp running natively in the browser via WebAssembly/Emscripten.

**Status (2026-07-09):** The 216 MB `.wasm` binary instantiates in the browser
and boots the guest call chain through CRT init + TU 160 data initialization.
~168 boot messages produced, 30+ real kernel stubs exercised, `main()` returns
cleanly. The browser launcher shows download progress, compilation/instantiation
phases, and full console output via Developer Tools.

Blockers for next milestone: the SDK kernel runtime (`kernel_state`, `module`,
`xthread`, etc.) is not built, so all indirect function calls resolve to `0x00000000`
(trap handler → return 0). The game cannot boot further without a populated
Import Address Table.

**PR:** https://github.com/moults31/nhl-legacy-recomp/pull/3 — `feat/wasm-browser-port`

---

## Current State

### What works (verified in browser)

| Layer | Status |
|-------|--------|
| **Compilation** | 181 generated recomp TUs + 6 WASM runtime files compile to 216 MB `.wasm` at `-Os`. |
| **SDK compatibility** | 5 SDK header patches + 1 SDK source patch (fiber layer) for Emscripten. |
| **Browser instantiation** | 256 MB initial memory + `ALLOW_MEMORY_GROWTH` (up to 4 GB). WASM instantiates via XHR + `WebAssembly.instantiate()`. |
| **Guest dispatch** | 129,934 guest functions registered in a 42 MB dispatch table. |
| **Data preloading** | 354 `.rdata`/`.data` section function pointers preloaded from `nhllegacy_functions.toml`. |
| **CRT init** | Guest entry point (`sub_82450000`) executes and returns. CRT init (`sub_82451038`) calls `RegisterLogCategory("cpu")` in 4043 ms. |
| **Kernel calls** | 30+ stubs exercised: `NtCreateFile`, `XeCryptSha`, `XeKeysConsolePrivateKeySign`, `ExCreateThread` (2 threads), `KeDelayExecutionThread`, `KeSetBasePriorityThread`, `RtlInitAnsiString`, `NtOpenFile` (5 calls), `NtReadFile`, `NtWriteFile`, `NtClose`, `RtlNtStatusToDosError`, `RtlTimeFieldsToTime`, `XAudioGetSpeakerConfig`, `XAudioRegisterRenderDriverClient`, `KeWaitForMultipleObjects`, `MmAllocatePhysicalMemoryEx`, `XamAlloc`. |
| **Boot chain** | 70+ TU 160 functions execute (data structure initialization). |
| **Browser launcher** | XHR-based WASM download with progress bar. Custom `instantiateWasm` callback with phase logging ("Downloading...", "Compiling...", "Instantiating...", "Running main()..."). Both DOM log div AND browser console output. Document title shows current status. Python HTTP server with COOP/COEP headers. |
| **Memory management** | Guest buffer uses bump allocator from WASM heap top (2.1 GB at address `0x4000000`). Dispatch table populated directly in guest memory. |

### What's built but not wired

| Component | Files | Status |
|-----------|-------|--------|
| **VFS** | `src/http_range_device.h/.cpp`, `src/lzx_decompress.h` | HTTP Range streaming device + LZX decompressor written. Not registered in boot sequence. |
| **Windowing** | `src/wasm/windowed_app_context_html5.h/.cpp`, `src/wasm/wasm_main.cpp` | HTML5 canvas windowing code written. Not linked into the build. |
| **WebGPU backend** | `src/wasm/webgpu_graphics_system.h/.cpp`, `src/wasm/webgpu_command_processor.h/.cpp` | Skeleton subclasses of SDK's GraphicsSystem/CommandProcessor. Compiles and links. |
| **Audio** | Design doc at `web/WASM_DESIGN.md` | SDL3 + FFmpeg wasm plan documented. Not implemented. |

### What doesn't work

| Issue | Impact | Root cause |
|-------|--------|------------|
| **IAT is empty** | Critical — all indirect calls go to `0x00000000` | The SDK's `Memory::InitializeFunctionTable` is not built. Without it, kernel import addresses are not populated in the guest-memory dispatch table. |
| **No kernel runtime** | High — guest cannot progress beyond CRT init | The SDK's kernel runtime (`kernel_state`, `xex_module`, `function_dispatcher`) depends on `rex::memory::Memory`, `rex::Runtime`, `rex::filesystem::VirtualFileSystem`, `rex::thread::*` — all require platform-specific internals (mmap, SEH, pthread signals) unavailable on WASM. |
| **Thread creation is a no-op** | High — game threads created with `start=0x00000000` | `ExCreateThread` stub reads thread params from guest memory but the kernel data structures haven't been initialized. |
| **File I/O returns dummy success** | Medium — game reads zeros from nonexistent files | `NtCreateFile`/`NtOpenFile`/`NtReadFile` stubs return 0 (STATUS_SUCCESS) with zero-length responses. |
| **XMA audio loop** | Medium — infinite loop when enabled | `XMACreateContext` stub returns 0 (success), causing infinite context creation. Fixed by returning incrementing non-zero IDs. (The `0x83095C48` init function is currently commented out in the boot chain.) |
| **No WebGPU rendering** | Medium — no visual output | The emdawnwebgpu port uses a new incompatible Dawn C API. The WebGPU skeleton needs to be mapped to this API. |
| **No asset delivery** | Medium — game can't load files | The VFS code exists but is not connected to `NtCreateFile`/`NtReadFile`. No server-side asset bundle. |

---

## Implementation Plan

### P0: Fix browser instantiation ✅ COMPLETED

**Goal:** The WASM binary instantiates and `main()` produces visible output in the browser.

**Status:** Done. The browser shows "WASM instantiated — main() finished" and the
full boot sequence appears in the developer console. Key results:

| File | Change |
|------|--------|
| `web/CMakeLists.txt` | Reduced `INITIAL_MEMORY` to 256 MB; changed compilation to `-Os` (fixes V8 7.3 MB function size limit) |
| `src/wasm/sdk_runtime.cpp` | `kGuestMaxOffset` reduced to 0x88000000 (~2.1 GB — covers full dispatch table); bump allocator from WASM heap top instead of `calloc` |
| `web/launcher.html` | Added custom `instantiateWasm` callback with XHR download + phase logging; `onAbort` shows error in status bar; console output mirrored to both DOM div and `console.log`/`console.error` |

**Build:** `docker run … nhl-legacy-recomp-web` (Docker image builds Emscripten 3.1.74 + CMake 3.22 + Ninja).

---

### P1: Verify guest output in browser ✅ COMPLETED

**Goal:** See the same boot sequence in the browser as in Node.js.

**Status:** Done. The developer console captures all ~168 boot messages including
`[sdk]` dispatch, kernel stubs, and TU 160 init chain output. The `onAbort`
handler, `onRuntimeInitialized`, and status-bar tracking all function correctly.

---

### P2: Build the SDK kernel runtime

**Goal:** The guest boots past CRT init into the game's main initialization loop
by providing a populated IAT, working thread creation, and real file I/O.

**Estimate:** 12–19 hours (mid-level engineer)

#### Background

The 14 SDK kernel files at `out/linux/deps/rexglue-sdk/src/system/` define the
real kernel runtime (`KernelState`, `XThread`, `XFile`, `Memory`, `FunctionDispatcher`,
etc.). They depend on `rex::memory::Memory`, `rex::Runtime`, `rex::filesystem::VirtualFileSystem`,
`rex::thread::*` — all of which use platform-specific primitives (mmap, SEH,
pthread signals) unavailable on WASM.

Our existing WASM infrastructure already solves the hard memory-mapping problem:
a bump-allocated guest buffer, a populated dispatch table, and `WamFunctionDispatcher`.
The task is to create WASM-compatible stubs for the `rex::*` symbols and wire them
to our existing infrastructure.

**Strategy:** Add one group of files at a time, collecting undefined symbols at
each step, creating minimal stubs in `sdk_runtime.cpp`. This incremental approach
avoids a monolithic link failure with 50+ unresolved symbols.

**Key dependency:** The SDK source files live at `out/linux/deps/rexglue-sdk/src/system/`,
not in the project's `src/` directory. The CMake variable `${SDK_DIR}` points to
the SDK root.

#### Slice 2a: Add core kernel files + Memory stubs

**Files to add to `WASM_SOURCES`:**
```
${SDK_DIR}/src/system/thread.cpp          (1 KB, 33 lines — minimal thread-local wrapper)
${SDK_DIR}/src/system/module.cpp           (3 KB, 98 lines — base Module class)
${SDK_DIR}/src/system/kernel_module.cpp    (4 KB — kernel export loader)
${SDK_DIR}/src/system/xobject.cpp          (15 KB — XObject base class)
${SDK_DIR}/src/system/xmodule.cpp          (3 KB, 106 lines — XModule inherits XObject)
${SDK_DIR}/src/system/kernel_state.cpp     (45 KB — central kernel state hub)
```

These 6 files are the minimal set needed to initialize kernel state and load
modules. They pull in the `Memory` interface but not the full MMU implementation.

**Expected undefined symbols** (to stub in `sdk_runtime.cpp`):

| Symbol | Stub behavior |
|--------|--------------|
| `rex::memory::Memory::SystemHeapAlloc(size, ...)` | `return malloc(size)` + track in a vector |
| `rex::memory::Memory::SystemHeapFree(addr, ...)` | `free(addr)` |
| `rex::memory::Memory::TranslateVirtual<T>(addr)` | `return (T*)(g_guest_base + addr)` |
| `rex::memory::Memory::virtual_membase()` | `return g_guest_base` |
| `rex::memory::Memory::InitializeFunctionTable` | See slice 2b below |
| `rex::memory::Memory::SetFunction` | Write PPCFunc* into dispatch table at `(guest_addr - code_base) * 2` |
| `rex::memory::Memory::DestroyFunctionTable` | No-op |
| `rex::memory::Memory::LookupHeap(addr)` | Return nullptr |
| `rex::kernel::xboxkrnl::KfAcquireSpinLock` / `KfReleaseSpinLock` | No-op (return 0) |
| `rex::cvar::RegisterFlag` / `UnregisterFlag` | No-op |
| `rex::filesystem::VirtualFileSystem::RegisterDevice` | No-op |
| `rex::filesystem::VirtualFileSystem::ResolvePath` | Return nullptr |
| `rex::runtime::ThreadState::Get` / `Bind` | Return nullptr / false |
| `rex::ppc::PPCFuncMappings[]` | Already provided by `generated/default/nhllegacy_init.cpp` |

**Also needed:** Add SDK source `include` path and crypto `include` path to
`NHL_WASM_INCLUDES`:
```
"${SDK_DIR}/src"
"${SDK_DIR}/thirdparty/crypto"
"${SDK_DIR}/thirdparty"
```

**Verification:** Build succeeds. `wasm_boot_guest()` reaches `InitializeFunctionTable` without crashing.

---

#### Slice 2b: Implement Memory::InitializeFunctionTable

**This is the key function that unblocks the guest from the CRT init loop.**
It populates the guest-memory Import Address Table (IAT), which resides at
`image_base + image_size = 0x82000000 + 0x1EA0000 = 0x83EA0000`.

Algorithm (backed by our existing bump-allocated guest buffer):
1. Map `code_base = 0x82450000`, `image_base = 0x82000000`, `image_size = 0x1EA0000` from PPCImageConfig
2. Compute `table_base = image_base + image_size = 0x83EA0000`
3. Clear `table_size = (code_size + 0x10000) * 2` bytes in guest buffer at `table_base`
4. Register the table info in a `function_tables_` vector
5. Then `SetFunction` is called 129,934 times by the caller to write each `PPCFunc*` at offset `(guest_addr - code_base) * 2` in the guest buffer

The existing `WamFunctionDispatcher::InitFromImage()` already does most of this.
The new implementation wraps it in the SDK's `Memory` interface so the 14 SDK
files can use it.

**Verification:** After `InitializeFunctionTable`, kernel import stubs appear at
non-zero addresses. The "unresolved indirect: 0x00000000" messages stop appearing.

---

#### Slice 2c: Add thread + sync primitives + remaining SDK files

**Files to add to `WASM_SOURCES`:**
```
${SDK_DIR}/src/system/xevent.cpp          (4 KB — manual/auto-reset events)
${SDK_DIR}/src/system/xsemaphore.cpp      (3 KB — counting semaphore)
${SDK_DIR}/src/system/xmutant.cpp         (3 KB — mutex)
${SDK_DIR}/src/system/xthread.cpp         (52 KB — ExCreateThread, KeWait, TLS, APC/DPC)
${SDK_DIR}/src/system/xfile.cpp           (13 KB — NtCreateFile, NtReadFile via VFS)
${SDK_DIR}/src/system/runtime.cpp         (12 KB — Runtime factory)
${SDK_DIR}/src/system/function_dispatcher.cpp  (13 KB — SDK's dispatcher)
${SDK_DIR}/src/system/xmemory.cpp         (82 KB, 2051 lines — full MMU — DON'T ADD)
```

**Note on `xmemory.cpp`:** The full Memory implementation uses mmap-backed virtual
memory, page protection tricks, and physical address mapping — none of which
works on WASM. We do NOT add this file. The existing WASM guest buffer +
bump allocator replaces it entirely. Only the `Memory` *interface* stubs
(slice 2a) are needed.

**Thread + sync primitive stubs** (single-threaded WASM):

| Symbol | Stub behavior |
|--------|--------------|
| `rex::thread::Event::CreateManualResetEvent` | Return pointer to dummy Event (always signaled) |
| `rex::thread::Event::CreateAutoResetEvent` | Same as manual |
| `rex::thread::Semaphore::Create(count, max)` | Return pointer to dummy Semaphore |
| `rex::thread::Mutant::Create` | Return pointer to dummy Mutant |
| `rex::thread::Mutex::lock` / `unlock` | No-op |
| `rex::thread::Thread` (full class) | Minimal stub: `Create()`, `Join()`, `SetAffinity()` all no-ops |

**Runtime + VFS stubs:**

| Symbol | Stub behavior |
|--------|--------------|
| `rex::Runtime::instance()` | Return static WASM Runtime singleton |
| `rex::Runtime::memory()` | Return our WASM Memory class reference |
| `rex::Runtime::function_dispatcher()` | Return our WamFunctionDispatcher wrapper |
| `rex::filesystem::VirtualFileSystem` ctor / methods | No-ops, return nullptr |
| `rex::filesystem::NullDevice` ctor | No-op |
| `rex::filesystem::HostPathDevice` ctor | No-op |
| `fmt::vformat_to` / `fmt::vformat` | Return empty string |

**Verification:** Build succeeds with all 13 SDK files (excluding xmemory.cpp).
Node.js boot output shows further progress with kernel state initialization.

---

#### Slice 2d: Resolve stub conflicts

When the SDK files are linked, their `REX_EXPORT` implementations for kernel
functions override the weak stubs in `kernel_stubs.cpp`. Expected replacements:

| Function | SDK provides | Action |
|----------|-------------|--------|
| `ExCreateThread` | Real implementation in xboxkrnl_threading.cpp | Keep our WASM-specific version (synchronous execution). The SDK version creates a real `XThread` object — not compatible with WASM's single-threaded model. |
| `KeDelayExecutionThread` | Real implementation | Accept SDK version (calls emscripten_sleep internally) |
| `KeWaitForMultipleObjects` | Real implementation | Accept SDK version (uses stubbed Event/Semaphore) |
| `NtCreateFile` / `NtReadFile` | Real implementation in xfile.cpp | Accept for P2 (returns dummy data); replace with VFS in P3 |
| `KeAcquireSpinLock` / `KeReleaseSpinLock` | Real implementation | Accept (lightweight — our stubs are no-ops, SDK's also compile to no-ops on WASM) |
| XAudio*, XMA*, Vd*, NetDll* | Stubbed in SDK (`REX_EXPORT_STUB`) | Keep our weak stubs (they match the SDK's stub level) |

**How to keep our ExCreateThread:** When adding xthread.cpp, conditionalize our
stub on `#ifndef USE_SDK_EXCREATETHREAD` or wrap it in a `__attribute__((weak))`
guarded by an `#if` so the SDK version is intentionally not linked.

---

#### Slice 2e: Fix XMA audio loop

**File:** `src/wasm/kernel_stubs.cpp`

```cpp
static std::atomic<unsigned> xma_context_id{1};
ctx.r3.u64 = xma_context_id.fetch_add(1);
```

The guest checks `ctx.r3.u64 != 0` for success. By returning incrementing IDs,
the guest thinks each XMA context was created successfully and the loop terminates.

**Follow-up:** Re-enable the `0x83095C48` init function in `wasm_boot_guest()`'s
boot chain (currently commented out with `// SKIP to avoid infinite loop`).

**Verification:** The function `0x83095C48` executes and returns without looping.
Total kernel calls do not increase by more than 100.

---

#### Slice 2f: End-to-end test

1. **Node.js:** `node nhllegacy.js` — verify boot output shows:
   - Kernel calls exceed 100 (vs current 30)
   - Thread creation shows non-zero start addresses
   - `NtOpenFile` returns proper NTSTATUS codes (not all zero)
   - No "unresolved indirect: 0x00000000" messages after IAT is populated

2. **Browser:** Serve and test. Verify the developer console shows:
   - All the above
   - `main()` returns cleanly (no crash)

#### Files to modify

| File | Change |
|------|--------|
| `web/CMakeLists.txt` | Add 13 SDK source files (all except xmemory.cpp); add SDK src + crypto include paths |
| `src/wasm/sdk_runtime.cpp` | Add Memory interface stubs (~200 lines); add InitializeFunctionTable wrapper; add ThreadState/Runtime/VFS stubs (~150 lines); add dummy Event/Semaphore/Mutant classes (~80 lines) |
| `src/wasm/kernel_stubs.cpp` | Fix XMACreateContext to return non-zero IDs; conditionally exclude ExCreateThread weak stub when SDK provides it |
| `src/wasm/sdk_runtime.h` | (New) Declare WASM-specific Memory subclass, dummy thread primitives |

---

### P3: Wire up the VFS

**Goal:** The guest can open and read game asset files via HTTP Range requests.

**Estimate:** 1–2 weeks

#### Background

The guest calls `NtCreateFile` / `NtReadFile` to load game assets (XEX header,
`.big` archives, texture data). Currently these stubs return 0 without providing
any data. We have already written `HttpRangeDevice` and `LzxDecompress` but they
are not connected to the kernel stubs or registered in the VFS.

#### Tasks

**3a. Create server-side asset extractor** (new script under `tools/`)

Write a Python script that:
1. Reads the `.big` archives using the existing QuickBMS extraction code
   from `tools/packager/src/installer.cpp`
2. Extracts boot/menu assets: `boot.big` (11 MB), `renderboot.big` (9 MB),
   `audioboot*.big` (~7 MB), `data0.big` (29 MB), `cacheboot.big` (32 MB)
   — total ~130 MB for boot-to-menu
3. Packs them into a single `.bundle` file with a JSON manifest:
   ```json
   [
     {"path": "rendering\\player\\texlib_0.rx2", "offset": 0, "size": 2048, "sha256": "..."},
     {"path": "fe\\ion\\mainmenu.bin", "offset": 2048, "size": 1024, "sha256": "..."}
   ]
   ```
4. Optionally LZX-compresses individual files to save space

**3b. Wire HttpRangeDevice into the VFS** (`src/wasm/sdk_runtime.cpp`)

In the boot sequence, after `preload_data_sections()`:
1. Fetch the manifest JSON from the server (URL configurable via env var)
2. Create an `HttpRangeDevice` with the bundle URL and manifest
3. Register it as the `game:` device via the SDK's VFS
4. Create a `HostPathDevice` backed by OPFS for `cache:` (saves, shader cache)

The `HttpRangeDevice` class is already implemented in `src/http_range_device.h/.cpp`.
It supports:
- HTTP Range requests for sub-file byte ranges
- LZX decompression of fetched chunks
- OPFS caching of decompressed data
- Entry tree synthesis from the manifest

**3c. Implement NtCreateFile/NtReadFile** (`src/wasm/kernel_stubs.cpp`)

Replace the current stubs with real implementations:
1. `NtCreateFile`: Parse the `OBJECT_ATTRIBUTES` from guest memory to get the
   file path. Open via `rex::filesystem::VirtualFileSystem::Open()`. Return a
   file handle (an integer index into a file table).
2. `NtReadFile`: Read from the file handle's data into guest memory at the
   specified guest address. Return the number of bytes read.
3. `NtClose`: Release the file handle.
4. `NtOpenFile`: Same as NtCreateFile but for existing files.

A simpler implementation for the POC: maintain a `std::unordered_map<uint32_t,
HttpRangeFile*>` of open file handles. On NtCreateFile, open via the VFS and
store in the map. On NtReadFile, read from the handle and copy to guest memory.

#### Files to modify

| File | Change |
|------|--------|
| `tools/web/extract_assets.py` | New: server-side asset extractor |
| `src/wasm/sdk_runtime.cpp` | Wire HttpRangeDevice into boot sequence |
| `src/wasm/kernel_stubs.cpp` | Implement NtCreateFile/NtReadFile/NtClose |

#### Junior engineer tasks

- **Task F:** Write `tools/web/extract_assets.py`. Use the existing
  `tools/packager/src/iso_source.cpp` XDVDFS reader as reference. Extract
  the 5 boot `.big` files. Produce a JSON manifest.
- **Task G:** Write the file handle table in `kernel_stubs.cpp`. Map
  NtCreateFile → file open, NtReadFile → read bytes, NtClose → close.

---

### P4: Implement WebGPU rendering

**Goal:** The guest framebuffer renders on the HTML5 canvas.

**Estimate:** 2–4 weeks

#### Background

We have skeleton `WebGpuGraphicsSystem` and `WebGpuCommandProcessor` classes
inheriting from the SDK's abstract `GraphicsSystem`/`CommandProcessor`. They
compile and link but all methods are stubbed. The emdawnwebgpu port installed
by Emscripten provides `webgpu/webgpu.h` but uses a new Dawn C API that is
incompatible with the older `wgpu.h` API.

**Key constraint:** We only need the **menu** to render for the boot-to-menu POC.
The menu is largely 2D UI/compositing — the SDK's FSI path (fragment-shader-interlock
for per-tile EDRAM resolves) is NOT needed. The guest writes a frontbuffer into
guest memory and calls `IssueSwap`. We just need to copy that frontbuffer to the
canvas.

#### Tasks

**4a. Map the emdawnwebgpu Dawn C API** (`src/wasm/webgpu_compat.h`)

Create a thin compatibility wrapper that maps the new Dawn C API to the
function signatures expected by the existing WebGPU skeleton code.

The key differences in the new API:
- `wgpuInstanceRequestAdapter(instance, options, WGPURequestAdapterCallbackInfo)` — uses a callback info struct instead of separate callback+userdata
- `WGPUSupportedLimits` renamed to `WGPULimits`
- `WGPURequiredLimits` — new struct for device creation
- Returns `WGPUFuture` instead of void

**4b. Implement WebGpuGraphicsSystem::InitializeWebGPU** (`src/wasm/webgpu_graphics_system.cpp`)

```cpp
bool InitializeWebGPU() {
  WGPUInstanceDescriptor desc = {};
  WGPUInstance instance = wgpuCreateInstance(&desc);
  if (!instance) return false;

  WGPURequestAdapterOptions opts = {};
  opts.powerPreference = WGPUPowerPreference_HighPerformance;
  
  // Async adapter request — use emscripten_sleep to wait
  bool done = false;
  WGPUAdapter adapter = nullptr;
  wgpuInstanceRequestAdapter(instance, &opts,
    {nullptr, nullptr, [](WGPURequestAdapterStatus s, WGPUAdapter a, char* m, void* ud) {
      auto* p = static_cast<std::pair<WGPUAdapter*, bool*>*>(ud);
      if (s == WGPURequestAdapterStatus_Success) *p->first = a;
      *p->second = true;
    }, &adapter});
  while (!done) emscripten_sleep(1);
  
  if (!adapter) { /* headless */ return true; }
  
  // Request device similarly
  // Store in wgpu_device_ member
  initialized_ = true;
  return true;
}
```

**4c. Implement WebGpuCommandProcessor::IssueSwap** (`src/wasm/webgpu_command_processor.cpp`)

This is the critical rendering function:
1. Read the guest framebuffer from guest memory at `frontbuffer_ptr`
2. Create a `WGPUTexture` with the framebuffer dimensions
3. Copy the framebuffer data (BGRA8 → RGBA8 if needed) into the texture
4. Create a `WGPURenderPipeline` with a fullscreen triangle vertex shader
   and a fragment shader that samples the framebuffer texture
5. Create a `WGPUCommandEncoder`, encode the draw, submit to the queue
6. Present the swapchain

For the initial POC, skip the render pipeline and just use
`wgpuQueueWriteTexture` to copy directly to the swapchain texture.

**4d. Write the WGSL shader** (embedded string in the .cpp)

Simple fullscreen blit:
```wgsl
@group(0) @binding(0) var fb_tex: texture_2d<f32>;
@group(0) @binding(1) var fb_samp: sampler;

@vertex
fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4<f32> {
  var pos = array<vec2<f32>, 3>(
    vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0)
  );
  return vec4(pos[i], 0.0, 1.0);
}

@fragment
fn fs(@builtin(position) coord: vec4<f32>) -> @location(0) vec4<f32> {
  return textureSample(fb_tex, fb_samp, coord.xy / vec2(1280.0, 720.0));
}
```

**4e. Connect to the guest's GPU command stream**

For the menu POC: the guest's `IssueSwap` calls are the only rendering events
we need. Each call provides the frontbuffer pointer, width, and height. Copy
that data to the canvas.

For full 3D rendering (later): the guest sends Xenos GPU commands (PM4 packets)
through the SDK's `CommandProcessor::ExecutePacket`. These need to be translated
to WebGPU draw calls, state changes, and texture uploads. This is the scope of
the full graphics backend — not needed for the boot-to-menu milestone.

#### Files to modify

| File | Change |
|------|--------|
| `src/wasm/webgpu_compat.h` | New: Dawn C API compatibility wrapper |
| `src/wasm/webgpu_graphics_system.cpp` | Implement device/adapter creation |
| `src/wasm/webgpu_command_processor.cpp` | Implement IssueSwap (framebuffer → canvas) |

#### Junior engineer tasks

- **Task H:** Read `webgpu/webgpu.h` from the emdawnwebgpu port. Document
  every function signature difference from the standard wgpu.h API.
- **Task I:** Write the WGSL shader as a C++ string literal. Test that it
  compiles with the Emscripten toolchain.

---

### P5: Polish and optimize

**Goal:** Production-quality browser experience.

**Estimate:** 1–2 weeks

#### Tasks

**5a. Reduce WASM binary size**
- The `-Os` optimization is already applied (216 MB). Try `-Oz` (aggressive size)
  or `-flto=thin` (Link-Time Optimization) to eliminate more dead code.
- Strip debug info with `-g0` (already set).
- Target: reduce from 216 MB to <100 MB.

**5b. Add keyboard/gamepad input**
- Map browser `keydown`/`keyup` events to Xbox 360 controller emulation.
- Use the Gamepad API for real controller support.
- Wire through `rex::input::InputSystem` stubs.

**5c. Add audio**
- Build FFmpeg (libavcodec) for WASM — the SDK uses it for XMA audio decode.
- Wire SDL3's WebAudio backend for audio output.
- Stub the audio initialization until the game reaches that phase.

**5d. Add settings persistence**
- Store `nhl_enhancements.ini` in OPFS or `localStorage`.
- Persist resolution, fullscreen, vsync settings.

**5e. Add proper loading screen**
- Show a styled loading screen with logo, progress percentage, and estimated time.
- Add a "Loading game assets..." phase after WASM init.

#### Files to modify

| File | Change |
|------|--------|
| `web/CMakeLists.txt` | Try optimization flags |
| `web/launcher.html` | Add input handling, audio, loading UI |
| `src/wasm/sdk_runtime.cpp` | Wire input stubs |

#### Junior engineer tasks

- **Task J:** Try `-Oz` in CMakeLists.txt. Report binary size. If it builds
  and runs in Node.js, test in browser.
- **Task K:** Add keyboard input to `web/launcher.html`. Map arrow keys +
  Enter to Xbox controller D-pad + A button.

---

## Summary

| Priority | Phase | What | Est. | Who | Status |
|----------|-------|------|------|-----|--------|
| **P0** | Browser inst. | Fix memory, add error handling, test | 4–8h | Any engineer | ✅ Done |
| **P1** | Guest output | Verify printErr, add logging | 2–4h | Any engineer | ✅ Done |
| **P2** | SDK kernel | Add SDK files, stub Memory/Thread/VFS symbols, fix IAT, fix loops | 12–19h | Mid-level | **Current** |
| **P3** | VFS | Asset extractor, HttpRangeDevice wiring, file stubs | 1–2w | Mid-level | Blocked by P2 |
| **P4** | WebGPU | Dawn API mapping, device init, framebuffer blit | 2–4w | Senior | Blocked by P2 |
| **P5** | Polish | Optimize size, input, audio, loading UI | 1–2w | Junior | Blocked by P4 |

### P2 Slice Breakdown

| Slice | What | Est. |
|-------|------|------|
| **P2a** | Add 6 core kernel files + Memory stubs in sdk_runtime.cpp | 3–5h |
| **P2b** | Implement Memory::InitializeFunctionTable (populate IAT) | 3–4h |
| **P2c** | Add 7 remaining SDK files + Thread/Runtime/VFS stubs | 3–5h |
| **P2d** | Resolve stub conflicts (ExCreateThread, kernel imports) | 1–2h |
| **P2e** | Fix XMA audio loop (return non-zero context IDs) | 0.5h |
| **P2f** | End-to-end test (Node.js + browser) | 2–3h |
| **Total P2** | | **12–19h** |

---

## Files in this PR

| File | Purpose |
|------|---------|
| `web/CMakeLists.txt` | Build config: generates `nhllegacy.wasm` + `.js` (256 MB initial memory, `-Os`, 13 SDK source files) |
| `web/Dockerfile` | Docker image for Emscripten build environment (3.1.74, cmake 3.22, ninja) |
| `web/build.sh` | Entry-point build script |
| `web/lib/compile.sh` | Shell-based compile script (alternative to CMake) |
| `web/launcher.html` | Browser HTML page with XHR download + progress bar, `instantiateWasm` callback with phase logging, mirrored console output |
| `web/serve.py` | Python HTTP server with COOP/COEP headers |
| `web/WASM_DESIGN.md` | Design doc for windowing/input/audio |
| `web/WEBGPU_BACKEND.md` | Design doc for WebGPU backend |
| `web/patches/rex/platform.h` | Patched: add `__EMSCRIPTEN__` platform detection |
| `web/patches/rex/platform/fpscr.h` | Patched: WASM FPSCR using SIMDE fallback |
| `web/patches/rex/chrono/chrono.h` | Patched: guard `clock_time_conversion` for missing libc++ feature |
| `web/patches/rex/chrono/chrono_steady_cast.h` | Patched: same libc++ guard |
| `web/patches/rex/ppc/intrinsics.h` | Patched: WASM paths for `simde_mm_vsl/vslo/vsro` |
| `web/patches/rex/core/fiber_posix.cpp` | Patched: WASM no-op fiber stubs |
| `web/patches/sys/eventfd.h` | Compatibility header: pthreads-based eventfd for WASM |
| `web/patches/emscripten_compat.h` | Linux API compat shims (syscall, madvise, etc.) |
| `src/wasm/sdk_runtime.cpp` | Core runtime: guest memory bump allocator, dispatch table, Memory/Thread/Runtime/VFS stubs, MMIO, clock, logging, data preloading, fake module handle, trap handler, boot sequence |
| `src/wasm/sdk_runtime.h` | New: WASM Memory subclass, dummy thread primitives, Event/Semaphore/Mutant stubs |
| `src/wasm/runtime_stubs.cpp` | Thin `main()` shim → `wasm_boot_guest()` |
| `src/wasm/kernel_stubs.cpp` | Auto-generated 311 kernel import stubs + smart ExCreateThread + fixed XMACreateContext |
| `src/wasm/windowed_app_context_html5.h/.cpp` | HTML5 canvas windowing |
| `src/wasm/wasm_main.cpp` | WASM entry point (alternative to runtime_stubs) |
| `src/wasm/webgpu_graphics_system.h/.cpp` | WebGPU GraphicsSystem skeleton |
| `src/wasm/webgpu_command_processor.h/.cpp` | WebGPU CommandProcessor skeleton |
| `src/http_range_device.h/.cpp` | HTTP Range streaming VFS device |
| `src/lzx_decompress.h` | Self-contained LZX decompressor |
| `docs/wasm-implementation-plan.md` | This document |
