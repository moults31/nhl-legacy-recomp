// VFS bridge — provides file operations for kernel stubs backed by the
// HttpRangeDevice (browser) or fallback stubs (Node.js).
//
// In the browser, init the bridge with InitWasmVfs() before any NtOpenFile
// calls.  On Node.js / no-asset builds the bridge returns STATUS_NO_SUCH_FILE.

#pragma once

#include <cstdint>

// Guest memory base — used by IssueSwap for framebuffer access
extern uint8_t* wasm_guest_base();

// Maximum number of concurrently open file handles.
static constexpr uint32_t kWasmMaxFileHandles = 64;

// Called during boot to set up the VFS (loads manifest, creates device).
// Returns true if game assets are available.
bool InitWasmVfs();

// Opens a file guest-side.  path points into guest memory (base + ...).
// Returns a handle (1..kWasmMaxFileHandles) on success, 0 on failure.
// Stores the NTSTATUS in out_status.
uint32_t WasmOpenFile(const char* guest_path, uint32_t& out_status);

// Reads from a previously opened handle into the guest buffer.
// Returns the number of bytes read, or 0 on end-of-file / error.
uint32_t WasmReadFile(uint32_t handle, void* buffer, uint32_t size);

// Closes an open handle.
void WasmCloseFile(uint32_t handle);
