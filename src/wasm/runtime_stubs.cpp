// WASM runtime entry point shim.
// All SDK runtime logic (dispatch, memory, stubs) lives in sdk_runtime.cpp.

#include <cstdio>

extern "C" int wasm_boot_guest();

int main() {
  std::fprintf(stderr, "[wasm] main() → wasm_boot_guest()\n");
  return wasm_boot_guest();
}
