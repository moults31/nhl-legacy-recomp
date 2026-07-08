# WASM build — produces nhllegacy.wasm from generated recomp + SDK runtime.
# Usage: source /tmp/emsdk/emsdk_env.sh && emcmake cmake -S web -B out/wasm/build -G Ninja
cmake_minimum_required(VERSION 3.25)
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
  "${SDK_DIR}/thirdparty/simde"
  "${SDK_DIR}/thirdparty/fmt/include"
  "${SDK_DIR}/thirdparty/spdlog/include"
  "${SDK_DIR}/thirdparty/tomlplusplus/include"
)

# ----- generated recomp library -----
file(GLOB GENERATED_SOURCES "${GENERATED_DIR}/*.cpp")
add_library(nhllegacy_recomp STATIC ${GENERATED_SOURCES})
target_include_directories(nhllegacy_recomp PRIVATE ${NHL_WASM_INCLUDES})
target_compile_options(nhllegacy_recomp PRIVATE -pthread -Wno-deprecated-pragma)
target_link_options(nhllegacy_recomp PRIVATE -pthread)

# ----- executable -----
set(WASM_SOURCES
  ${WASM_SRC}/runtime_stubs.cpp
  ${WASM_SRC}/sdk_runtime.cpp
  ${WASM_SRC}/kernel_stubs.cpp
  ${WASM_SRC}/windowed_app_context_html5.cpp
)

add_executable(nhllegacy ${WASM_SOURCES})
target_include_directories(nhllegacy PRIVATE
  ${NHL_WASM_INCLUDES}
  ${WASM_SRC}
  ${SRC_DIR}
  ${SDK_DIR}/src
  ${CMAKE_SOURCE_DIR}/..  # repo root (for generated/default/ relative includes)
)
target_compile_options(nhllegacy PRIVATE -pthread -Wno-deprecated-pragma)
target_link_libraries(nhllegacy PRIVATE nhllegacy_recomp)

target_link_options(nhllegacy PRIVATE
  -pthread
  -O0
  -g0
  -sWASM_BIGINT
  -sALLOW_MEMORY_GROWTH=1
  -sMAXIMUM_MEMORY=4294967296
  -sINITIAL_MEMORY=3221225472
  -sSTACK_SIZE=4MB
  -sERROR_ON_UNDEFINED_SYMBOLS=0
  -Wl,--allow-undefined
)
