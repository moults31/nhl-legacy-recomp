#include "src/wasm/webgpu_graphics_system.h"

#include <utility>

#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "src/wasm/webgpu_command_processor.h"

namespace rex::graphics::webgpu {

WebGpuGraphicsSystem::WebGpuGraphicsSystem() {}

WebGpuGraphicsSystem::~WebGpuGraphicsSystem() {}

std::string WebGpuGraphicsSystem::name() const {
  return "WebGPU";
}

void WebGpuGraphicsSystem::CreateProvider(bool with_presentation) {
  // TODO(Phase 4): Create WebGpuProvider with instance/adapter/device.
  // For now no provider exists — presentation will be unavailable.
  (void)with_presentation;
  REXGPU_WARN("CreateProvider — stubbed, no WebGPU device created");
}

std::unique_ptr<CommandProcessor> WebGpuGraphicsSystem::CreateCommandProcessor() {
  return std::make_unique<WebGpuCommandProcessor>(this, kernel_state_);
}

}  // namespace rex::graphics::webgpu
