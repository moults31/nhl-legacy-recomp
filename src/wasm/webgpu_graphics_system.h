// WebGPU graphics system for NHL Legacy WASM port.
// No-op skeleton — WebGPU initialization deferred until the
// emdawnwebgpu (Dawn native) API is properly integrated.

#pragma once

#include <memory>
#include <string>

#include <rex/graphics/graphics_system.h>

namespace rex::graphics::webgpu {

class WebGpuGraphicsSystem : public rex::graphics::GraphicsSystem {
 public:
  WebGpuGraphicsSystem();
  ~WebGpuGraphicsSystem() override;

  static bool IsAvailable();

  std::string name() const override { return "WebGPU (skeleton)"; }

 protected:
  void CreateProvider(bool with_presentation) override;

 private:
  std::unique_ptr<rex::graphics::CommandProcessor> CreateCommandProcessor() override;
};

}  // namespace rex::graphics::webgpu
