#include "src/wasm/webgpu_command_processor.h"

#include <cstdio>
#include <cstring>

#include <rex/logging.h>

namespace rex::graphics::webgpu {

WebGpuCommandProcessor::WebGpuCommandProcessor(
    GraphicsSystem* graphics_system,
    system::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state) {}

WebGpuCommandProcessor::~WebGpuCommandProcessor() = default;

bool WebGpuCommandProcessor::SetupContext() {
  // TODO(Phase 4): Request WebGPU adapter + device, configure surface.
  // The browser async model means this likely chains out before
  // the main loop starts. For now, mark as initialized without a device.
  context_initialized_ = true;
  REXGPU_WARN("SetupContext — no WebGPU device (stub)");
  return true;
}

void WebGpuCommandProcessor::ShutdownContext() {
  // TODO(Phase 4): Release WGPUDevice, WGPUAdapter, WGPUInstance.
  context_initialized_ = false;
}

Shader* WebGpuCommandProcessor::LoadShader(
    xenos::ShaderType shader_type,
    uint32_t guest_address,
    const uint32_t* host_address,
    uint32_t dword_count) {
  // TODO(Phase 4): Translate Xenos ucode → SPIR-V → WGSL.
  // Menu doesn't use guest shaders; returning nullptr is safe.
  (void)shader_type;
  (void)guest_address;
  (void)host_address;
  (void)dword_count;
  return nullptr;
}

bool WebGpuCommandProcessor::IssueDraw(
    xenos::PrimitiveType prim_type,
    uint32_t index_count,
    IndexBufferInfo* index_buffer_info,
    bool major_mode_explicit) {
  // Menu does not issue 3D draw calls.
  // TODO(Phase 5/full 3D): implement render pipeline dispatch.
  (void)prim_type;
  (void)index_count;
  (void)index_buffer_info;
  (void)major_mode_explicit;
  return false;
}

bool WebGpuCommandProcessor::IssueCopy() {
  // Menu does not issue EDRAM resolve copies.
  // TODO(Phase 5): implement when 3D rendering is needed.
  return false;
}

void WebGpuCommandProcessor::IssueSwap(
    uint32_t frontbuffer_ptr,
    uint32_t frontbuffer_width,
    uint32_t frontbuffer_height) {
  // TODO(Phase 4): Read guest front buffer from GPU emulation memory,
  // upload to a staging buffer, and blit to the swapchain texture
  // via WebGPU command encoder.
  //
  // Minimal implementation:
  //   uint8_t* fb = memory_->TranslatePhysical(frontbuffer_ptr);
  //   // upload fb → WGPUTexture
  //   // render fullscreen quad sampling the texture
  //   // queue submit + surface present
  //
  // For now, log the swap request so we know the guest is alive.
  static int swap_count = 0;
  if (swap_count < 5) {
    REXGPU_INFO("IssueSwap stub — fb=0x{:08X} {}x{} (showing first 5 only)",
                frontbuffer_ptr, frontbuffer_width, frontbuffer_height);
    swap_count++;
  }
}

void WebGpuCommandProcessor::TracePlaybackWroteMemory(
    uint32_t base_ptr,
    uint32_t length) {
  // No trace support in WASM.
  (void)base_ptr;
  (void)length;
}

void WebGpuCommandProcessor::RestoreEdramSnapshot(const void* snapshot) {
  // No EDRAM in menu path.
  (void)snapshot;
}

}  // namespace rex::graphics::webgpu
