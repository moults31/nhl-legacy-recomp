# nhllegacy - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
# C is enabled for the plume shader-embed (.c blobs from plume_file_to_c); CXX is the project.
project(nhllegacy LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Linux native build: skip high-cut plume + D3D12 deps (Vulkan path only).
option(NHLLEGACY_VULKAN_ONLY "Skip high-cut plume/D3D12 deps (Vulkan production path)" OFF)
if(NOT WIN32 AND NOT DEFINED NHLLEGACY_VULKAN_ONLY_DEFAULTED)
  set(NHLLEGACY_VULKAN_ONLY ON CACHE BOOL "Skip high-cut plume/D3D12/glslang deps (Vulkan production path)" FORCE)
  set(NHLLEGACY_VULKAN_ONLY_DEFAULTED TRUE CACHE INTERNAL "")
endif()

include(generated/rexglue.cmake)

# Sources
set(NHLLEGACY_SOURCES
    src/main.cpp
    src/kernel_stubs.cpp
    src/diag_hooks.cpp
    # Intel hybrid (P/E-core) thread placement: pins the process default CPU set to
    # the Performance cores so the recomp's heavy threads don't land on E-cores.
    # Env-gated (NHL_PIN_PCORES, default ON); no-op on AMD / non-hybrid CPUs. Called
    # from NhllegacyApp::OnPreSetup before any threads spin up.
    src/cpu_affinity.cpp
    # High-cut engine (H-1): D3D9 logical resource graph — pass-through hooks that
    # build the EDRAM-free RT/texture/resolve graph. Env-gated (NHL_HIGHCUT); no
    # behavior change. Supersedes the M1 correlation tap (gpu/hooks/d3d9_tap.cpp,
    # kept in-tree for reference) — both override the same symbols, so only one may
    # be compiled into the target at a time.
    gpu/hooks/d3d9_resources.cpp
    # Animation capture: hooks the DctAnimationAsset decode chain (sub_82CA9B20 ->
    # sub_82CA9618 -> sub_82CA7BD0) to record decoded poses. Env-gated
    # (NHL_ANIM_CAPTURE); pass-through when unset. Overrides distinct symbols from
    # the D3D9 hooks above, so it composes with them. See docs cba-format.md §6.
    gpu/hooks/anim_capture.cpp
    # Headless animation-decode harness (Windows VirtualQuery scan) — appended on WIN32.
    # High-cut engine (H-2): first plume-driven frame — an in-process plume D3D12
    # swapchain driven by the guest Present hook. Env-gated (NHL_HIGHCUT_PRESENT).
    # High-cut sources appended below when not NHLLEGACY_VULKAN_ONLY
    # Loose-file overlay: union-mount _compiled over the cache: scratch dir,
    # with per-file .dds -> .rx2 texture synthesis in the lower tree.
    src/union_device.cpp
    src/loose_tree_device.cpp
    # Stage-1 texture-injection registry + sidecar (auto address->asset mapping).
    src/injection_registry.cpp
    # Trace replay + image dump, shared with tools/replay. Enables the in-host
    # replay-and-screenshot mode (NHL_REPLAY_XTR=<frame.xtr>): replays a captured
    # frame through the live backend + presenter and writes replay_frame.png.
    tools/replay/src/xtr_player.cpp
    tools/replay/src/image_dump.cpp
)

# Windows-only sources (D3D12 injection + VirtualQuery anim scan).
if(WIN32)
  list(APPEND NHLLEGACY_SOURCES
      src/anim_decode.cpp
      renderer/core/nhl_graphics_system.cpp
      renderer/core/nhl_command_processor.cpp)
endif()


set(NHLLEGACY_HIGHCUT_SOURCES
    gpu/hooks/plume_present.cpp
    gpu/hooks/highcut_spirv_probe.cpp
    gpu/spirv/spirv_builder.cc
    gpu/spirv/spirv_shader_translator.cc
    gpu/spirv/spirv_shader_translator_alu.cc
    gpu/spirv/spirv_shader_translator_fetch.cc
    gpu/spirv/spirv_shader_translator_memexport.cc
    gpu/spirv/spirv_shader_translator_rb.cc
    gpu/spirv/spirv_shader.cc
    gpu/spirv/spirv_translator_tables.cc
)
if(NOT NHLLEGACY_VULKAN_ONLY)
  list(APPEND NHLLEGACY_SOURCES ${NHLLEGACY_HIGHCUT_SOURCES})
endif()

if(WIN32)
    add_executable(nhllegacy WIN32 ${NHLLEGACY_SOURCES})
else()
    add_executable(nhllegacy ${NHLLEGACY_SOURCES})
endif()

rexglue_setup_target(nhllegacy)

# So TUs can stub symbols from sources excluded under NHLLEGACY_VULKAN_ONLY.
if(NHLLEGACY_VULKAN_ONLY)
  target_compile_definitions(nhllegacy PRIVATE NHLLEGACY_VULKAN_ONLY=1)
endif()

# SPIKE (docs/vulkan-rov-backend-spike-prompt.md): the SDK's Vulkan ROV backend is
# a COMPILE-TIME option (REXGLUE_USE_VULKAN, OFF in the stock win-amd64 zips). When
# CMAKE_PREFIX_PATH points at an SDK built with it ON, enable this to compile the
# NHL_VK_BACKEND env gate (src/nhllegacy_app.h) and link the Vulkan graphics system.
option(NHLLEGACY_VULKAN_BACKEND "Link the SDK's Vulkan ROV backend (needs an SDK built with REXGLUE_USE_VULKAN=ON)" OFF)
if(NHLLEGACY_VULKAN_BACKEND)
    target_compile_definitions(nhllegacy PRIVATE NHL_HAVE_VULKAN_BACKEND=1)
    # Compiled ONLY in the Vulkan build dir — links the SDK's Vulkan backend symbols
    # that the default (D3D12-only) SDK does not export.
    target_sources(nhllegacy PRIVATE renderer/core/nhl_vk_backend.cpp)
    # In-game enhancements/settings overlay (Guide-button toggle + perf HUD +
    # supersampling). VK-gated: it reads the Vulkan fps tap and is only created
    # under NHL_VK_BACKEND. Uses the SDK ImGui drawer (rexui/imgui, already linked).
    target_sources(nhllegacy PRIVATE renderer/core/nhl_overlay.cpp)
    # Guest controller-input block while the overlay is open (overrides the guest
    # XamInputGetState import). VK-build-only so the default D3D12 build's input
    # path is unchanged.
    target_sources(nhllegacy PRIVATE src/input_block.cpp)
    message(STATUS "nhllegacy: Vulkan backend gate ENABLED (NHL_HAVE_VULKAN_BACKEND)")
endif()

if(NOT NHLLEGACY_VULKAN_ONLY)
# High-cut engine (H-2): vendored plume RHI (MIT, third_party/plume). Built as a
# static lib in its own directory scope (C++17, its own backends) and linked into
# nhllegacy so gpu/hooks/plume_present.cpp can drive an in-process plume swapchain
# from the intercepted guest Present. Same add_subdirectory pattern proven by the
# standalone gpu/smoke build.
add_subdirectory(third_party/plume ${CMAKE_BINARY_DIR}/plume_build)
target_link_libraries(nhllegacy PRIVATE plume d3d12 dxgi dxguid)

# High-cut C-1: compile the bring-up triangle shader to SPIR-V (+DXIL) via plume's own
# dxc-fetch + embed cmake. Generates ${CMAKE_BINARY_DIR}/shaders/triangle{Vert,Frag}.hlsl.
# spirv.h (blobs triangle{Vert,Frag}BlobSPIRV) and adds the embedded .c to nhllegacy +
# ${CMAKE_BINARY_DIR} to its include path, so gpu/hooks/plume_present.cpp can render real
# geometry (proves plume's pipeline/vertex-buffer/draw path beyond the H-2 clears). The
# REAL path-C engine generates SPIR-V at runtime from Xenos ucode via the SDK
# SpirvShaderTranslator; this hardcoded triangle is bring-up only.
include(third_party/plume/examples/cmake/PlumeShaders.cmake)
plume_shaders_init()
plume_compile_vertex_shader(nhllegacy
    "${CMAKE_CURRENT_SOURCE_DIR}/gpu/hooks/shaders/triangle.hlsl" triangleVert VSMain)
plume_compile_pixel_shader(nhllegacy
    "${CMAKE_CURRENT_SOURCE_DIR}/gpu/hooks/shaders/triangle.hlsl" triangleFrag PSMain)
# C-3a: zero-input solid-color PS to pair with a translated Xenos VS for pipeline creation.
plume_compile_pixel_shader(nhllegacy
    "${CMAKE_CURRENT_SOURCE_DIR}/gpu/hooks/shaders/solid.hlsl" solidFrag PSMain)

endif()

# glslang 14.x (SpvBuilder.h): required by SDK Vulkan headers and high-cut SPIR-V port.
# Distro glslang-dev (15+) no longer ships SpvBuilder.h.
if(NHLLEGACY_VULKAN_BACKEND OR NOT NHLLEGACY_VULKAN_ONLY)
  set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
  set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
  set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
  set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  if(UNIX AND NOT APPLE)
    # Linux native build: Docker image bakes glslang 14.3.0 + P-3 patch at /opt/glslang.
    if(NOT EXISTS "/opt/glslang/SPIRV/SpvBuilder.h")
      message(FATAL_ERROR
        "Linux builds require image-baked glslang at /opt/glslang. "
        "Use: ISO=/path/to/game.iso ./linux/build.sh")
    endif()
    set(_nhl_glslang_src "/opt/glslang")
    message(STATUS "nhllegacy: using image-baked glslang at /opt/glslang")
    if(NOT TARGET SPIRV)
      add_subdirectory("${_nhl_glslang_src}" "${CMAKE_BINARY_DIR}/glslang_build" EXCLUDE_FROM_ALL)
    endif()
  else()
    # Windows: FetchContent glslang 14.3.0 and apply the high-cut P-3 patch.
    include(FetchContent)
    FetchContent_Declare(glslang
        GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
        GIT_TAG 14.3.0)
    FetchContent_MakeAvailable(glslang)
    set(_nhl_glslang_src "${glslang_SOURCE_DIR}")
    set(_glslang_spvbuilder "${_nhl_glslang_src}/SPIRV/SpvBuilder.cpp")
    if(EXISTS "${_glslang_spvbuilder}")
      file(READ "${_glslang_spvbuilder}" _spvbuilder_src)
      string(FIND "${_spvbuilder_src}" "nhl high-cut P-3" _spvbuilder_patched)
      if(_spvbuilder_patched EQUAL -1)
        string(REPLACE
          "    Id typeId = getResultingAccessChainType();"
[[    Id typeId = getTypeId(base); /* nhl high-cut P-3: stateless access-chain type */
    typeId = getContainedTypeId(typeId);
    for (int nhl_i = 0; nhl_i < (int)offsets.size(); ++nhl_i)
        typeId = isStructType(typeId) ? getContainedTypeId(typeId, getConstantScalar(offsets[nhl_i]))
                                      : getContainedTypeId(typeId, offsets[nhl_i]);]]
          _spvbuilder_src "${_spvbuilder_src}")
        file(WRITE "${_glslang_spvbuilder}" "${_spvbuilder_src}")
        message(STATUS "high-cut P-3: patched glslang createAccessChain (stateless)")
      endif()
    endif()
  endif()
  target_link_libraries(nhllegacy PRIVATE SPIRV)
  target_include_directories(nhllegacy PRIVATE "${_nhl_glslang_src}")
endif()

# snappy (for .xtr trace decompression in the in-host replay mode). Statically
# linked into rexruntime.dll but not re-exported, so link it directly; lib name
# is config-specific. SDK lib dir derived from the package config location.
get_filename_component(_rexsdk_lib "${rexglue_DIR}/../.." ABSOLUTE)
if(WIN32)
  target_link_libraries(nhllegacy PRIVATE
      $<$<CONFIG:Debug>:${_rexsdk_lib}/snappyd.lib>
      $<$<CONFIG:RelWithDebInfo>:${_rexsdk_lib}/snappyrd.lib>
      $<$<CONFIG:Release>:${_rexsdk_lib}/snappy.lib>
      $<$<CONFIG:MinSizeRel>:${_rexsdk_lib}/snappy.lib>
  )
else()
  target_link_libraries(nhllegacy PRIVATE
      $<$<CONFIG:Debug>:${_rexsdk_lib}/libsnappyd.a>
      $<$<CONFIG:RelWithDebInfo>:${_rexsdk_lib}/libsnappyrd.a>
      $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:${_rexsdk_lib}/libsnappy.a>
  )
endif()

# rex/ui/renderdoc_api.h does `#include <renderdoc_app.h>`, which lives in the
# SDK's include/renderdoc subdir (not on the default -isystem include path). Add
# it so the beta backend's programmatic RenderDoc capture (NHL_BETA_RENDERDOC) can
# bracket the takeover frame.
target_include_directories(nhllegacy PRIVATE "${_rexsdk_lib}/../include/renderdoc")

# Runtime .dds -> .rx2 texture conversion: link the Rust static lib built from
# nhl-database-studio/crates/tdb-rx2-ffi (reuses the proven tdb-rx2 format code
# rather than re-porting the tiling/byte-swap/descriptor math into C++).
# Build it with: cargo build -p tdb-rx2-ffi --release   (in nhl-database-studio)
set(RX2FFI_LIB "E:/Repositories/nhl-database-studio/target/release/tdb_rx2_ffi.lib"
    CACHE FILEPATH "Path to the tdb-rx2-ffi static library")
if(WIN32 AND EXISTS "${RX2FFI_LIB}")
    target_compile_definitions(nhllegacy PRIVATE NHLLEGACY_HAVE_RX2FFI=1)
    # Rust staticlib pulls in these Windows system libs.
    target_link_libraries(nhllegacy PRIVATE "${RX2FFI_LIB}"
        ntdll userenv ws2_32 dbghelp bcrypt advapi32)
else()
    message(WARNING "tdb-rx2-ffi not found at ${RX2FFI_LIB}; loose .dds texture "
                    "override disabled (build it: cargo build -p tdb-rx2-ffi --release)")
endif()

# End-user packager CLI (disc dump -> playable install). See tools/packager/.
# Windows-only for now: it ships the Windows port binaries and uses a wide
# (wmain) entry point.
option(NHLLEGACY_BUILD_PACKAGER "Build the nhl-legacy-builder packager CLI" ON)
if(NHLLEGACY_BUILD_PACKAGER AND WIN32)
    enable_testing()
    add_subdirectory(tools/packager)
endif()

# Offline trace replay tooling (renderer plan, Phase 4).
# Self-written .xtr player: the SDK's TraceDump/TracePlayer/TraceReader are NOT
# exported from rexruntime.dll, so tools/replay parses the trace itself and
# drives it through the *exported* CommandProcessor API (ExecutePacket /
# RestoreRegisters / RestoreEdramSnapshot) on a headless Runtime + our backend.
option(NHLLEGACY_BUILD_TRACE_TOOLS "Build the offline .xtr replay tool (nhl-trace-replay)" ON)
if(NHLLEGACY_BUILD_TRACE_TOOLS AND WIN32)
    add_subdirectory(tools/replay)
endif()
