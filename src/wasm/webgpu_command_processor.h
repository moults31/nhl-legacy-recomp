// WebGPU command processor — skeleton. No-op pending emdawnwebgpu integration.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <rex/graphics/command_processor.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/xenos.h>

namespace rex::graphics::webgpu {

class WebGpuCommandProcessor : public CommandProcessor {
 public:
  WebGpuCommandProcessor(GraphicsSystem* graphics_system,
                         system::KernelState* kernel_state);
  ~WebGpuCommandProcessor() override;

 protected:
  Shader* LoadShader(xenos::ShaderType shader_type, uint32_t guest_address,
                     const uint32_t* host_address, uint32_t dword_count) override;

  bool IssueDraw(xenos::PrimitiveType prim_type, uint32_t index_count,
                 IndexBufferInfo* index_buffer_info, bool major_mode_explicit) override;

  bool IssueCopy() override;

  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                 uint32_t frontbuffer_height) override;

  void TracePlaybackWroteMemory(uint32_t base_ptr, uint32_t length) override;

  void RestoreEdramSnapshot(const void* snapshot) override;

  bool SetupContext() override;
  void ShutdownContext() override;

 private:
  bool context_initialized_ = false;
};

}  // namespace rex::graphics::webgpu
