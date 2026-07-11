// WebGPU graphics system — skeleton. No-op until emdawnwebgpu API is integrated.

#include "webgpu_graphics_system.h"
#include "webgpu_command_processor.h"

#include <cstdio>

#include <rex/logging.h>

namespace rex::graphics::webgpu {

WebGpuGraphicsSystem::WebGpuGraphicsSystem() = default;
WebGpuGraphicsSystem::~WebGpuGraphicsSystem() = default;

bool WebGpuGraphicsSystem::IsAvailable() {
  return true;  // skeleton always available (no-ops)
}

void WebGpuGraphicsSystem::CreateProvider(bool with_presentation) {
  (void)with_presentation;
  std::fprintf(stderr, "[webgpu] CreateProvider — skeleton (no GPU init yet)\n");
}

std::unique_ptr<rex::graphics::CommandProcessor>
WebGpuGraphicsSystem::CreateCommandProcessor() {
  return std::make_unique<WebGpuCommandProcessor>(this, kernel_state_);
}

}  // namespace rex::graphics::webgpu
