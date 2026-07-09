# WASM Port — Implementation Plan

## Overview

NHL Legacy Recomp running natively in the browser via WebAssembly/Emscripten.
Status: the recompiled Xbox 360 code compiles to a 216 MB `.wasm` binary, boots
the guest's CRT init chain in Node.js, and calls 30+ real kernel functions.
The browser launcher downloads and compiles the WASM but hangs at "Running..."
(instantiation failure due to 3.5 GB initial memory).

This document describes the complete path from current state to a playable
browser-rendered game.

**PR:** https://github.com/moults31/nhl-legacy-recomp/pull/3 — `feat/wasm-browser-port`

---

## Current State

### What works

| Layer | Status |
|-------|--------|
| **Compilation** | 183 generated recomp TUs compile to WASM via Emscripten. Build: 216 MB `.wasm`, ~15 min with Ninja@16. |
| **SDK compatibility** | 5 SDK header patches + 1 SDK source patch (fiber layer) for Emscripten. |
| **Guest dispatch** | 129,934 guest functions registered in a 42 MB dispatch table. |
| **Data preloading** | 354 `.rdata`/`.data` section function pointers preloaded from `nhllegacy_functions.toml`. |
| **CRT init** | Guest entry point (`sub_82450000`) executes and returns. CRT init function (`sub_82451038`) calls `RegisterLogCategory("cpu")`. |
| **Kernel calls** | 30+ kernel functions called: `NtCreateFile`, `XeCryptSha`, `XeKeysConsolePrivateKeySign`, `ExCreateThread`, `KeDelayExecutionThread`, `KeSetBasePriorityThread`, `KeWaitForMultipleObjects`, `RtlInitAnsiString`, `NtOpenFile`, `RtlNtStatusToDosError`, `RtlTimeFieldsToTime`, `XAudioGetSpeakerConfig`, `XAudioRegisterRenderDriverClient`, `MmAllocatePhysicalMemoryEx`, `MmGetPhysicalAddress`, `XMACreateContext`, `XamAlloc`. |
| **Boot chain** | 70+ TU 160 functions execute (data structure initialization). |
| **MMIO handler** | Stub implementations for `CheckLoad`/`CheckStore` that log and return 0. |
| **Browser launcher** | HTML page with progress bar, console redirection, canvas element. Python HTTP server with COOP/COEP headers. |
| **Node.js test** | `node nhllegacy.js` produces the full boot sequence output immediately. |

### What's built but not wired

| Component | Files | Status |
|-----------|-------|--------|
| **VFS** | `src/http_range_device.h/.cpp`, `src/lzx_decompress.h` | HTTP Range streaming device + LZX decompressor written. Not registered in the boot sequence. |
| **Windowing** | `src/wasm/windowed_app_context_html5.h/.cpp`, `src/wasm/wasm_main.cpp` | HTML5 canvas windowing code written. Not linked into the build. |
| **WebGPU backend** | `src/wasm/webgpu_graphics_system.h/.cpp`, `src/wasm/webgpu_command_processor.h/.cpp` | Skeleton subclasses of SDK's GraphicsSystem/CommandProcessor with stubbed methods. Compiles and links. |
| **Audio** | Design doc at `web/WASM_DESIGN.md` | SDL3 + FFmpeg wasm plan documented. Not implemented. |

### What doesn't work

| Issue | Impact | Root cause |
|-------|--------|------------|
| **Browser shows "Running..." indefinitely** | Critical — no visible output in browser | `-sINITIAL_MEMORY=3758096384` (3.5 GB) causes `WebAssembly.instantiate()` to fail silently. The browser cannot create WASM linear memory that large at instantiation time. |
| **`calloc(3.25 GB)`** in the guest | High — would fail even if instantiation succeeded | The guest buffer is 3.25 GB. With 216 MB WASM code, total exceeds 4 GB linear memory limit on wasm32. |
| **No kernel import resolution** | High — guest stuck in CRT init | The SDK's kernel runtime (`kernel_state`, `xex_module`, `function_dispatcher`) is not built. Without it, all indirect calls go to `0x00000000` (unresolved vtable pointers). |
| **XMA audio loop** | Medium — guest loops forever | `XMACreateContext` stub returns success immediately, causing the guest to create contexts in an infinite loop. |
| **No WebGPU rendering** | Medium — no visual output | The emdawnwebgpu port uses a new incompatible Dawn C API. The WebGPU skeleton needs to be mapped to this API. |
| **No asset delivery** | Medium — game can't load files | The VFS code exists but is not connected to `NtCreateFile`/`NtReadFile`. No server-side asset bundle. |

---

## Implementation Plan

### P0: Fix browser instantiation

**Goal:** The WASM binary instantiates and `main()` produces visible output in the browser.

**Estimate:** 4–8 hours

#### Tasks

1. **Reduce initial memory** (`web/CMakeLists.txt`):
   - Change `-sINITIAL_MEMORY=3758096384` to `-sINITIAL_MEMORY=268435456` (256 MB).
   - Keep `-sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=4294967296` so the heap grows on demand.

2. **Reduce guest buffer** (`src/wasm/sdk_runtime.cpp`):
   - Change `kGuestMaxOffset` from `0xD0000000` (3.25 GB) to `0x40000000` (1 GB).
   - The CRT init chain only accesses addresses below ~2 GB. If the guest accesses
     beyond 1 GB, the access will OOB — add a check: if `calloc(1 GB)` fails,
     fall back to 256 MB and log a warning.

3. **Add WASM instantiation error handling** (`web/launcher.html`):
   - Add a custom `Module.instantiateWasm` callback that logs success/failure.
   - Add a `Module.onAbort` handler that displays the error visibly on the page.
   - Add logging for each step: "Fetching...", "Compiling...", "Instantiating...", "Running main()...".

4. **Test**:
   - Rebuild with reduced memory settings.
   - Test in Node.js first: `node nhllegacy.js` should still produce output.
   - Serve and test in browser. If successful, the guest boot sequence will
     appear in the log div. If instantiation still fails, the error message
     will indicate why (memory limit, compilation failure, etc.).

#### Files to modify

| File | Change |
|------|--------|
| `web/CMakeLists.txt` | Reduce `INITIAL_MEMORY` to 256 MB |
| `src/wasm/sdk_runtime.cpp` | Reduce `kGuestMaxOffset` to 1 GB, add calloc fallback |
| `web/launcher.html` | Add instantiateWasm callback, onAbort handler, step logging |

#### Junior engineer tasks

- **Task A:** Reduce the two memory constants, rebuild, serve, and test in browser.
  Report: does the browser show output or an error? If error, what message?

---

### P1: Verify guest output in browser

**Goal:** See the same boot sequence in the browser as in Node.js.

**Estimate:** 2–4 hours

#### Tasks

1. **Confirm `Module.printErr` receives stderr output.** The guest uses
   `fprintf(stderr, ...)` which Emscripten routes through `Module.printErr`.
   Add a visible prefix to confirm: `printErr: msg => log('[stderr] ' + msg, 'err')`.

2. **Log when `callMain()` is reached.** After runtime init, the JS calls `callMain()`
   which invokes our `main()` function. Log before and after.

3. **Handle calloc failure gracefully.** If the 1 GB `calloc` fails in the browser,
   fall back to a smaller buffer (256 MB). The guest will crash on high-address
   accesses, but we'll see output up to that point.

#### Files to modify

| File | Change |
|------|--------|
| `web/launcher.html` | Add Module.preRun logging, improve printErr |
| `src/wasm/sdk_runtime.cpp` | Add calloc fallback to 256 MB |

#### Junior engineer tasks

- **Task B:** Update the HTML to log each step. Test in browser.
  Does "WASM runtime initialized" appear? Does "main() called" appear?

---

### P2: Build the SDK kernel runtime

**Goal:** The guest boots past CRT init into the game's main initialization loop.

**Estimate:** 1–2 weeks

#### Background

The guest's CRT init completes (70+ TU 160 functions execute), but the game
cannot boot further because:

1. **Import Address Table (IAT) is empty:** All indirect function calls resolve
   to `0x00000000` because the SDK's `Memory::InitializeFunctionTable` has not
   populated the guest-memory dispatch table with kernel import addresses.

2. **Thread scheduling is stubbed:** `ExCreateThread` reads the start address
   from guest memory but doesn't actually create a thread context or call the
   start function.

3. **File I/O returns dummy data:** `NtCreateFile` and `NtReadFile` return 0
   (success) but don't provide any file data. The guest reads zeros from the
   files it tries to open.

4. **XMA audio loop:** `XMACreateContext` returns success immediately, causing
   the guest to allocate ~49,000 XMA contexts in an infinite loop.

#### Tasks

**2a. Add SDK system source files to the build** (`web/CMakeLists.txt`)

We verified the following 14 SDK files compile with 0 errors under Emscripten:

```
src/system/kernel_state.cpp
src/system/runtime.cpp
src/system/thread.cpp
src/system/xthread.cpp
src/system/xmemory.cpp
src/system/xobject.cpp
src/system/xevent.cpp
src/system/xsemaphore.cpp
src/system/xmutant.cpp
src/system/xfile.cpp
src/system/module.cpp
src/system/xmodule.cpp
src/system/function_dispatcher.cpp
src/system/kernel_module.cpp
```

Add them to the `WASM_SOURCES` list in `web/CMakeLists.txt`. Fix any
compile-time errors. When both our stubs and the SDK's real implementations
define the same symbol, remove the stub (the SDK implementation is the
authoritative one).

Also add the `crypto/TinySHA1.hpp` include path: add
`-I${SDK_DIR}/thirdparty/crypto` and `-I${SDK_DIR}/thirdparty` to
`NHL_WASM_INCLUDES`.

**2b. Stub undefined symbols** (`src/wasm/sdk_runtime.cpp`)

The SDK files pull in ~50 undefined symbols from the core library
(which we can't compile due to deep Linux API dependencies). Create stubs:

| Symbol | Stub behavior |
|--------|--------------|
| `rex::memory::Memory::SystemHeapAlloc` | `return malloc(size)` |
| `rex::memory::Memory::SystemHeapFree` | `free(ptr)` |
| `rex::memory::Memory::InitializeFunctionTable` | Populate IAT from PPCFuncMappings (see 2c) |
| `rex::memory::Memory::SetFunction` | Write function pointer into guest's dispatch table |
| `rex::memory::Memory::DestroyFunctionTable` | No-op |
| `rex::filesystem::VirtualFileSystem` | Stub all methods (return null/empty) |
| `rex::filesystem::NullDevice` | Stub constructor |
| `rex::filesystem::HostPathDevice` | Stub constructor |
| `rex::cvar::RegisterFlag` / `UnregisterFlag` | No-op |
| `rex::kernel::xboxkrnl::*SpinLock*` | No-op (return 0) |
| `rex::runtime::ThreadState::Get` / `Bind` | Return nullptr |
| `fmt::vformat_to` / `fmt::vformat` | Return empty string |

Collect the full list by running the linker and parsing the error output.
Use `llvm-nm` on the failing object files to find all undefined symbols.

**2c. Implement `Memory::InitializeFunctionTable`**

This is the **key function** that unblocks the guest from the CRT init loop.
It populates the guest's Import Address Table (IAT) in guest memory.

Algorithm:
1. Iterate through `PPCFuncMappings[]` (129,934 entries)
2. For each kernel import symbol (`__imp__*`), determine its guest address
3. Write the function pointer into the guest-memory dispatch table
   at `base + IMAGE_BASE + IMAGE_SIZE + (guest_addr - CODE_BASE) * 2`

The dispatch table format: `uintptr_t table[(guest_addr - CODE_BASE) * 2]`
where `CODE_BASE = 0x82450000` and `IMAGE_BASE + IMAGE_SIZE = 0x83EA0000`.

**2d. Fix `ExCreateThread`** (`src/wasm/kernel_stubs.cpp`)

The current stub reads the start address from guest memory at
`params_ptr + 0x150` but only logs it. Make it actually execute:
1. Read the full `XTHREAD_CREATION_PARAMS` struct from guest memory
   (offset 0x150 for `start_address`, offset 0x14C for `xapi_thread_startup`)
2. If `xapi_thread_startup` is non-zero, call it as a trampoline, passing
   `start_address` as argument (r3)
3. Create a `PPCContext` with stack at `0x40000000` and TLS at `0x10000000`
4. Call the function and log the return value
5. Return 0 (success) in `ctx.r3`

**2e. Fix the XMA audio loop** (`src/wasm/kernel_stubs.cpp`)

Modify the `__imp__XMACreateContext` stub to return a valid context handle:
```cpp
static std::atomic<unsigned> xma_context_id{1};
ctx.r3.u64 = xma_context_id.fetch_add(1);
```
The guest checks if the return value is 0 (error). By returning non-zero
incremental IDs, the guest thinks each context was created successfully and
the loop terminates.

#### Files to modify

| File | Change |
|------|--------|
| `web/CMakeLists.txt` | Add 14 SDK source files, add crypto include paths |
| `src/wasm/sdk_runtime.cpp` | Add ~50 stub definitions for SDK core symbols |
| `src/wasm/kernel_stubs.cpp` | Fix ExCreateThread, fix XMACreateContext loop |

#### Junior engineer tasks

- **Task C:** Add the 14 SDK files to CMakeLists.txt. Run the build. Collect
  all compile/link errors. Document which files need patches.
- **Task D:** Run `llvm-nm -u` on the link-failing object files. Collect all
  undefined symbol names. Create stub definitions for each in `sdk_runtime.cpp`.
- **Task E:** Fix the `XMACreateContext` stub to return non-zero IDs. Verify
  the loop terminates (no more than 1,000 kernel calls from that function).

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
- Try `-O1` (optimize for speed) or `-Os` (optimize for size) at compile time.
  Keep `--profiling-funcs` if wasm-opt OOMs.
- Try `-flto=thin` (Link-Time Optimization) to eliminate dead code.
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

- **Task J:** Try `-Os` in CMakeLists.txt. Report binary size. If it builds
  and runs in Node.js, test in browser.
- **Task K:** Add keyboard input to `web/launcher.html`. Map arrow keys +
  Enter to Xbox controller D-pad + A button.

---

## Summary

| Priority | Phase | What | Est. | Who |
|----------|-------|------|------|-----|
| **P0** | Browser inst. | Fix memory, add error handling, test | 4–8h | Any engineer |
| **P1** | Guest output | Verify printErr, add logging | 2–4h | Any engineer |
| **P2** | SDK kernel | Add source files, stub symbols, fix IAT, fix loops | 1–2w | Mid-level |
| **P3** | VFS | Asset extractor, HttpRangeDevice wiring, file stubs | 1–2w | Mid-level |
| **P4** | WebGPU | Dawn API mapping, device init, framebuffer blit | 2–4w | Senior |
| **P5** | Polish | Optimize size, input, audio, loading UI | 1–2w | Junior |

### Junior engineer task list

| Task | Phase | Description | Skills |
|------|-------|-------------|--------|
| A | P0 | Reduce memory, rebuild, test in browser, report | CMake, JS, browser dev tools |
| B | P1 | Update HTML for step logging, test in browser | JS, HTML |
| C | P2 | Add 14 SDK files to CMakeLists.txt, fix compile errors | C++, CMake |
| D | P2 | Collect undefined symbols, create stubs in sdk_runtime.cpp | C++, linkers |
| E | P2 | Fix XMACreateContext stub (return non-zero IDs) | C++ |
| F | P3 | Write `tools/web/extract_assets.py` (extract .big files) | Python, file formats |
| G | P3 | Write file handle table in kernel_stubs.cpp | C++, VFS |
| H | P4 | Document emdawnwebgpu API differences from standard wgpu.h | C, WebGPU |
| I | P4 | Write WGSL shader as C++ string literal | WGSL, WebGPU |
| J | P5 | Try `-Os` optimization, report binary size | CMake, WASM |
| K | P5 | Add keyboard input to launcher.html | JS, DOM events |

---

## Files in this PR

| File | Purpose |
|------|---------|
| `web/CMakeLists.txt` | Build config: generates `nhllegacy.wasm` + `.js` |
| `web/Dockerfile` | Docker image for Emscripten build environment |
| `web/build.sh` | Entry-point build script |
| `web/lib/compile.sh` | Shell-based compile script (alternative to CMake) |
| `web/launcher.html` | Browser HTML page with progress bar, canvas, console |
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
| `src/wasm/sdk_runtime.cpp` | Core runtime: dispatch table, guest memory, MMIO, clock, logging, data preloading, fake module handle, trap handler, boot sequence |
| `src/wasm/runtime_stubs.cpp` | Thin `main()` shim → `wasm_boot_guest()` |
| `src/wasm/kernel_stubs.cpp` | Auto-generated 311 kernel import stubs + smart ExCreateThread |
| `src/wasm/windowed_app_context_html5.h/.cpp` | HTML5 canvas windowing |
| `src/wasm/wasm_main.cpp` | WASM entry point (alternative to runtime_stubs) |
| `src/wasm/webgpu_graphics_system.h/.cpp` | WebGPU GraphicsSystem skeleton |
| `src/wasm/webgpu_command_processor.h/.cpp` | WebGPU CommandProcessor skeleton |
| `src/http_range_device.h/.cpp` | HTTP Range streaming VFS device |
| `src/lzx_decompress.h` | Self-contained LZX decompressor |
| `docs/implementation-plan.md` | This document |
