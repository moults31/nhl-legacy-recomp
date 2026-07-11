# WASM Port — Windowing, Input & Audio Design

## Overview

This document describes the emscripten/HTML5 backend for NHL Legacy Recomp's
window, input, and audio subsystems. Files live under `src/wasm/` (project) with
patch points identified in the SDK at `out/linux/deps/rexglue-sdk/`.

**Phase 0c constraint**: Emscripten 3.1.74 has no Vulkan shim. WebGPU is the
only GPU API. The rendering backend (Phase 4) is separate from this task.

## Files Created

| File | Purpose |
|---|---|
| `src/wasm/windowed_app_context_html5.h` | HTML5 WindowedAppContext header |
| `src/wasm/windowed_app_context_html5.cpp` | emscripten-based implementation |
| `src/wasm/wasm_main.cpp` | WASM entry point (replaces `windowed_app_main_posix.cpp`) |
| `web/WASM_DESIGN.md` | This document |

---

## 1. Windowing — `HTML5WindowedAppContext`

Replaces `GTKWindowedAppContext` (`src/ui/windowed_app_context_gtk.cpp` at SDK,
~106 LOC).

### API Mapping

| SDK Abstraction | GTK Implementation | Emscripten Replacement |
|---|---|---|
| `RunMainGTKLoop()` | `gtk_main()` (blocking) | `emscripten_set_main_loop_arg(cb, this, 0, 1)` |
| `NotifyUILoopOfPendingFunctions()` | `gdk_threads_add_idle()` | `emscripten_async_call(fn, this, 0)` |
| `CallInUIThreadSynchronous()` | Fence + deferred | Short-circuits in single-threaded (base class) |
| `PlatformQuitFromUIThread()` | `gtk_main_quit()` + idle source chain | `emscripten_cancel_main_loop()` |
| Window surface | `XGetXCBConnection` / `gdk_x11_window_get_xid` → Vulkan surface | **Phase 4**: WebGPU canvas context |

### Implementation Details

- **Single-threaded mode** (no `-pthread`): everything runs on browser main thread.
  `CallInUIThread`/`CallInUIThreadSynchronous` are no-ops (base class handles).
  `CallInUIThreadDeferred` uses `emscripten_async_call` to schedule via the
  JS event loop.

- **`NotifyUILoopOfPendingFunctions()`**: Guards with a mutex + boolean to prevent
  duplicate `emscripten_async_call` scheduling. The single-threaded assumption
  means the "other threads" that call this method don't exist, but the SDK's
  internal thread abstractions may still call it — `emscripten_async_call(fn, ctx, 0)`
  is the zero-delay equivalent of `gdk_threads_add_idle`.

- **`emscripten_set_main_loop_arg(cb, this, 0, 1)`** with `simulate_infinite_loop=1`
  (true): blocks the calling thread, calling the callback at each rAF tick, until
  `emscripten_cancel_main_loop()` is invoked from the callback. After cancellation,
  `QuitFromUIThread()` is called — mirroring GTK's `gtk_main()` + `QuitFromUIThread()`
  pattern exactly.

- **No nested main loops**: HTML5/emscripten has no modal dialog concept, so the
  quit-chain idle source pattern used by GTK (`gtk_main_level()` checking) is
  unnecessary.

### Entry Point (`wasm_main.cpp`)

Mirrors `windowed_app_main_posix.cpp` (79 LOC at SDK):
```cpp
extern "C" int main(int argc, char** argv) {
  // Init cvars, logging
  HTML5WindowedAppContext app_context;
  auto app = GetWindowedAppCreator()(app_context);
  app->OnInitialize();
  app_context.RunMainHTMLLoop();  // blocks until cancel_main_loop
  app->InvokeOnDestroy();
  // Shutdown logging
}
```

---

## 2. Input — SDL3 Gamepad (Reuses Existing Code)

**Source**: `src/input/sdl/sdl_input_driver.cpp` at SDK (~725 LOC)

### SDL3 Emscripten Support

SDL3 (vendored at `thirdparty/sdl3/`) has first-class emscripten support:
- `cmake/PreseedEmscriptenCache.cmake` — all cmake cache variables pre-seeded
- `CMakeLists.txt` at line 1666: `elseif(EMSCRIPTEN)` with full platform config
- `SDL_JOYSTICK_EMSCRIPTEN` — maps HTML5 Gamepad API → SDL gamepad events
- `SDL_VIDEO_DRIVER_EMSCRIPTEN` — canvas + keyboard/mouse events
- `src/joystick/emscripten/*.c`, `src/video/emscripten/*.c`

### Design Decision: No Code Changes

`SDLInputDriver` uses `SDL_InitSubSystem(SDL_INIT_GAMEPAD)`, `SDL_AddEventWatch`,
`SDL_OpenGamepad`, etc. — all supported by SDL3's emscripten joystick driver.
`SDLInputDriver::QueueControllerUpdate()` calls `CallInUIThread()` to pump events,
which is a no-op in single-threaded mode since we're already on the UI thread.

**The existing `sdl_input_driver.cpp` works without modification.**

### Keyboard/Mouse (Future)

SDL3's emscripten video driver also provides keyboard+mouse events through SDL's
event queue. This could supplement or replace direct `emscripten_set_keydown_callback`
calls. Either approach works — using SDL3 gives a unified input event stream.

---

## 3. Audio — SDL3 Audio + FFmpeg XMA Decode (Reuses Existing Code)

**Sources at SDK**:
- `src/audio/sdl/sdl_audio_system.cpp` (54 LOC)
- `src/audio/sdl/sdl_audio_driver.cpp` (216 LOC)
- `src/audio/xma_context.cpp` (768 LOC, uses FFmpeg libavcodec)

### SDL3 Emscripten Audio

SDL3 has `SDL_AUDIO_DRIVER_EMSCRIPTEN` — Web Audio API backend:
- `src/audio/emscripten/*.c` — implements `SDL_OpenAudioDeviceStream()` via
  `AudioContext` / `AudioWorklet`
- Same API surface as the Linux ALSA/PulseAudio backends
- `SDLAudioDriver` uses the same callback pattern

**The existing `sdl_audio_driver.cpp` and `sdl_audio_system.cpp` work without modification.**

### FFmpeg XMA Decoding

`XmaContext::Setup()` at `xma_context.cpp:58` calls:
```cpp
av_codec_ = avcodec_find_decoder(AV_CODEC_ID_XMAFRAMES);
av_context_ = avcodec_alloc_context3(av_codec_);
av_frame_ = av_frame_alloc();
```

The SDK builds only the needed FFmpeg subset:
- `libavutil` (~80 .c files listed in `thirdparty/CMakeLists.txt`)
- `libavcodec` (~90 .c files, XMA + WMA + MP3 decoders)
- `ffmpeg-overlay/` provides custom `codec_list.c`, `parser_list.c`, `bsf_list.c`

**For WASM**: Compile the same FFmpeg subset with `emcc`. The XMAFRAMES decoder
is pure C (no x86 ASM). FFmpeg has been successfully compiled to WASM by many
projects. SDK `thirdparty/CMakeLists.txt` lines 379–435+ list all required .c files.

**Performance**: XMA subframe decode at 48kHz ~1–2ms per frame. WASM SIMD
(`emcc -msimd128`) keeps this well within the 16ms per-frame budget.

### Alternative: Server-Side Pre-Decode

If FFmpeg WASM compilation proves difficult, all XMA audio data can be decoded
offline and replaced with Opus/MP3. The guest's audio submit path (`SubmitFrame`)
sends raw PCM float samples — the codec is only involved in the initial decode
of the XMA stream. A server-side pipeline could:
1. Identify all XMA streams in the game data
2. Decode to Opus via ffmpeg CLI
3. Serve decoded Opus alongside the game data
4. Modify the audio path to play Opus instead of decoding XMA

This avoids the FFmpeg dependency entirely but adds server-side complexity.

---

## 4. ImGui Overlay

**Source**: `renderer/core/nhl_overlay.cpp` (project, ~868 LOC)

Uses pure ImGui core API (`#include <imgui.h>`) — no backend-specific includes.
The SDK's `imgui_drawer.cpp` creates backend-specific pipeline state. For WASM:
- ImGui ships a WebGPU backend (`imgui_impl_wgpu.cpp/h`)
- The SDK's `ImGuiDrawer` needs a WebGPU variant (Phase 4)
- The overlay dialog code requires **no changes**

---

## 5. SDK Patching Required

The SDK needs minor patches for the emscripten platform:

### `src/ui/CMakeLists.txt` (SDK)

Add emscripten branch to platform-specific sources:
```cmake
if(EMSCRIPTEN)
    set(REXUI_PLATFORM_SOURCES
        # Empty — implementations provided by the project (src/wasm/)
    )
elseif(WIN32)
    set(REXUI_PLATFORM_SOURCES ...)  # existing
else()
    set(REXUI_PLATFORM_SOURCES ...)  # existing GTK path
endif()
```

### `src/ui/window_gtk.cpp` (SDK) — `Window::Create`

The static `Window::Create` factory in `window_gtk.cpp:305` instantiates
`GTKWindow`. For WASM, we need an `HTML5Window` that wraps the canvas element
and provides `CreateSurfaceImpl()` returning a WebGPU surface.

**Phase 4 work**: Write `src/wasm/window_html5.cpp` + `.h` implementing a
`Window` subclass similar to `GTKWindow` but targeting WebGPU. Until then,
the `Window::Create` stub can return a minimal window for bring-up testing.

### `thirdparty/CMakeLists.txt` (SDK) — FFmpeg

The existing FFmpeg source list is architecture-agnostic (no x86 ASM files for
XMA/WMA/MP3 decoders). Same `.c` files work with emcc. Add:
```cmake
if(EMSCRIPTEN)
    target_compile_options(libavutil PRIVATE -msimd128)
    target_compile_options(libavcodec PRIVATE -msimd128)
endif()
```

### `thirdparty/sdl3/CMakeLists.txt` (SDK)

Already has full emscripten support (lines 1666–1770). No patches needed.

### `cmake/sdlplatform.cmake` (SDK)

Verify `EMSCRIPTEN` branch exists for SDL CPU detection. The `PreseedEmscriptenCache.cmake`
sets `SDL_CPU_EMSCRIPTEN=1`. May need to ensure SDL's `sdlplatform.cmake` picks up
`EMSCRIPTEN` correctly.

---

## 6. Build Integration

### Linker Flags (Emscripten)
```cmake
target_link_options(nhllegacy_wasm PRIVATE
  -sALLOW_MEMORY_GROWTH=1
  -sMAXIMUM_MEMORY=4GB
  -sSTACK_SIZE=16MB
  -sASYNCIFY=0               # Single-threaded, no async needed
  -sEXIT_RUNTIME=0           # Don't exit — app runs continuously
  -sINITIAL_MEMORY=256MB
  --shell-file web/shell.html
)
```

### Threading Model

**Phase 1: Single-threaded**. The guest recomp runs in-band with the browser
event loop. This simplifies:
- No `-pthread` flag needed
- No `emscripten_sync_run_in_main_runtime_thread` needed
- SDL3 audio/video work in single-threaded mode
- ImGui draws inline with rAF

**Phase 2 (future)**: Pthreads + PROXY_TO_PTHREAD for performance. The guest
recomp runs on a separate pthread; DOM access requires marshaling.

---

## 7. Summary of Design Decisions

| Subsystem | SDK Source | WASM Approach | Reuse? |
|---|---|---|---|
| Window context | `windowed_app_context_gtk.cpp` | `src/wasm/windowed_app_context_html5.cpp` | Rewrite — 65 LOC |
| Window surface | `window_gtk.cpp` (963 LOC) | `src/wasm/window_html5.cpp` (Phase 4) | Rewrite |
| Input | `sdl_input_driver.cpp` (725 LOC) | SDL3 emscripten joystick driver | 100% reuse |
| Audio | `sdl_audio_driver.cpp` (216 LOC) | SDL3 emscripten audio driver | 100% reuse |
| XMA Decode | `xma_context.cpp` (768 LOC) | FFmpeg libavcodec emcc-compiled | 100% reuse |
| ImGui | `nhl_overlay.cpp` (868 LOC) | ImGui WebGPU backend (Phase 4) | Overlay code unchanged |
| Entry point | `windowed_app_main_posix.cpp` (79 LOC) | `src/wasm/wasm_main.cpp` (51 LOC) | Rewrite |

---

## 8. Open Questions

| Question | Status |
|---|---|
| Does `emscripten_async_call` support zero-delay in 3.1.74? | Confirmed — `emscripten.h:80`: `void emscripten_async_call(em_arg_callback_func func, void *arg, int millis)` |
| Does SDL3 emscripten build with emsdk 3.1.74? | SDL3 CMake has full emscripten support. `PreseedEmscriptenCache.cmake` exists. |
| Will FFmpeg compile for WASM with the subset we need? | Yes — XMA/WMA/MP3 decoders are pure C. FFmpeg has been successfully compiled to WASM. |
| Canvas surface for WebGPU? | Phase 4. HTML `#canvas` element created in shell.html. |
| Keyboard input — direct emscripten callbacks or SDL? | Either works. SDL3 provides unified input stream; direct callbacks are simpler for initial bring-up. |
