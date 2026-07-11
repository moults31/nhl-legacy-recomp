// WebGPU command processor — IssueSwap copies guest framebuffer to 2D canvas.

#include "webgpu_command_processor.h"
#include "webgpu_graphics_system.h"

#include <cstdio>
#include <cstring>
#include <atomic>

#include <emscripten.h>
#include <rex/logging.h>
#include <rex/graphics/xenos.h>
#include "vfs_bridge.h"

namespace rex::graphics::webgpu {

WebGpuCommandProcessor::WebGpuCommandProcessor(
    GraphicsSystem* graphics_system,
    rex::system::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state) {}

WebGpuCommandProcessor::~WebGpuCommandProcessor() {
  ShutdownContext();
}

bool WebGpuCommandProcessor::SetupContext() {
  if (context_initialized_) return true;
  context_initialized_ = true;
  std::fprintf(stderr, "[webgpu:cpu] context initialized (skeleton — no GPU)\n");
  return true;
}

void WebGpuCommandProcessor::ShutdownContext() {
  context_initialized_ = false;
}

rex::graphics::Shader* WebGpuCommandProcessor::LoadShader(
    xenos::ShaderType shader_type, uint32_t guest_address,
    const uint32_t* host_address, uint32_t dword_count) {
  (void)shader_type; (void)guest_address; (void)host_address; (void)dword_count;
  return nullptr;
}

bool WebGpuCommandProcessor::IssueDraw(
    xenos::PrimitiveType prim_type, uint32_t index_count,
    IndexBufferInfo* index_buffer_info, bool major_mode_explicit) {
  (void)prim_type; (void)index_count; (void)index_buffer_info; (void)major_mode_explicit;
  return false;
}

bool WebGpuCommandProcessor::IssueCopy() {
  return false;
}

void WebGpuCommandProcessor::IssueSwap(
    uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
    uint32_t frontbuffer_height) {
  static std::atomic<unsigned> swap_count{0};
  auto n = swap_count.fetch_add(1, std::memory_order_relaxed);
  if (n == 0 || (n % 60) == 0) {
    std::fprintf(stderr, "[webgpu] swap #%u: fb=0x%08X %ux%u\n",
                 n + 1, frontbuffer_ptr, frontbuffer_width, frontbuffer_height);
  }

  uint8_t* buf = wasm_guest_base() + frontbuffer_ptr;
  char js[2048];
  snprintf(js, sizeof(js),
    "var p=%u,w=%u,h=%u,ctx=Module.canvas2d;"
    "if(ctx){var a=new Uint8ClampedArray(HEAPU8.buffer,p,w*h*4);"
    "for(var i=0;i<a.length;i+=4){var b=a[i],r=a[i+2];a[i]=r;a[i+2]=b;}"
    "var img=new ImageData(a,w,h);ctx.putImageData(img,0,0);}",
    (unsigned)(uintptr_t)buf, frontbuffer_width, frontbuffer_height);
  emscripten_run_script(js);
}

void WebGpuCommandProcessor::TracePlaybackWroteMemory(
    uint32_t base_ptr, uint32_t length) {
  (void)base_ptr; (void)length;
}

void WebGpuCommandProcessor::RestoreEdramSnapshot(const void* snapshot) {
  (void)snapshot;
}

}  // namespace rex::graphics::webgpu
