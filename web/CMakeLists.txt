# WASM build — produces nhllegacy.wasm from generated recomp + SDK runtime.
# Usage: source /tmp/emsdk/emsdk_env.sh && emcmake cmake -S web -B out/wasm/build -G Ninja
cmake_minimum_required(VERSION 3.22)
project(nhllegacy-wasm LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GENERATED_DIR "${CMAKE_SOURCE_DIR}/../generated/default")
set(SDK_DIR "${CMAKE_SOURCE_DIR}/../out/linux/deps/rexglue-sdk")
set(PATCH_DIR "${CMAKE_SOURCE_DIR}/patches")
set(WASM_SRC "${CMAKE_SOURCE_DIR}/../src/wasm")
set(SRC_DIR "${CMAKE_SOURCE_DIR}/../src")

# Include paths (patched headers take precedence)
set(NHL_WASM_INCLUDES
  "${PATCH_DIR}"
  "${GENERATED_DIR}"
  "${SDK_DIR}/include"
  "${SDK_DIR}/src"       # SDK internal headers
  "${SDK_DIR}/thirdparty/simde"
  "${SDK_DIR}/thirdparty/fmt/include"
  "${SDK_DIR}/thirdparty/spdlog/include"
  "${SDK_DIR}/thirdparty/tomlplusplus/include"
  "${SDK_DIR}/thirdparty/crypto"
  "${SDK_DIR}/thirdparty"       # for "crypto/TinySHA1.hpp" include paths
)

# ----- generated recomp library -----
file(GLOB GENERATED_SOURCES "${GENERATED_DIR}/*.cpp")
add_library(nhllegacy_recomp STATIC ${GENERATED_SOURCES})
target_include_directories(nhllegacy_recomp PRIVATE ${NHL_WASM_INCLUDES})
target_compile_options(nhllegacy_recomp PRIVATE -O1 -pthread -Wno-deprecated-pragma)

# ----- executable -----
set(SDK_SYSTEM_SRC "${SDK_DIR}/src/system")
set(WASM_SOURCES
  ${WASM_SRC}/runtime_stubs.cpp
  ${WASM_SRC}/kernel_stubs.cpp
  ${WASM_SRC}/sdk_runtime.cpp
  ${WASM_SRC}/windowed_app_context_html5.cpp
  ${WASM_SRC}/webgpu_graphics_system.cpp
  ${WASM_SRC}/webgpu_command_processor.cpp
  ${SRC_DIR}/http_range_device.cpp
  # P2a: core kernel runtime — compile cleanly under Emscripten
  ${SDK_SYSTEM_SRC}/thread.cpp
  ${SDK_SYSTEM_SRC}/module.cpp
  ${SDK_SYSTEM_SRC}/xmodule.cpp
  ${SDK_SYSTEM_SRC}/kernel_module.cpp
  ${SDK_SYSTEM_SRC}/xobject.cpp
  ${SDK_SYSTEM_SRC}/kernel_state.cpp
  # P2c: remaining SDK system files (function_dispatcher excluded — its
  # ResolveIndirectFunction conflicts with our WASM implementation, and its
  # GetBoundFunctionDispatcher needs Runtime::Setup which isn't ready yet)
  ${SDK_SYSTEM_SRC}/export_resolver.cpp
  ${SDK_SYSTEM_SRC}/xevent.cpp
  ${SDK_SYSTEM_SRC}/xsemaphore.cpp
  ${SDK_SYSTEM_SRC}/xmutant.cpp
  ${SDK_SYSTEM_SRC}/xfile.cpp
  ${SDK_SYSTEM_SRC}/xthread.cpp
  ${SDK_SYSTEM_SRC}/runtime.cpp
)

add_executable(nhllegacy ${WASM_SOURCES})
target_include_directories(nhllegacy PRIVATE
  ${NHL_WASM_INCLUDES}
  ${WASM_SRC}
  ${SRC_DIR}
  ${SDK_DIR}/src
  ${CMAKE_SOURCE_DIR}/..  # repo root (for generated/default/ relative includes)
)
target_compile_options(nhllegacy PRIVATE -Wno-deprecated-pragma)
target_link_libraries(nhllegacy PRIVATE nhllegacy_recomp)

target_link_options(nhllegacy PRIVATE
  -O1
  -g0
  -sWASM_BIGINT
  -sALLOW_MEMORY_GROWTH=1
  -sMAXIMUM_MEMORY=4294967296
  -sINITIAL_MEMORY=268435456
  -sSTACK_SIZE=4MB
  -sERROR_ON_UNDEFINED_SYMBOLS=0
  -Wl,--allow-undefined
  --profiling-funcs
)
