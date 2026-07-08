#pragma once

#include <memory>
#include <string>

#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>

namespace rex::graphics::webgpu {

class WebGpuGraphicsSystem : public GraphicsSystem {
 public:
  WebGpuGraphicsSystem();
  ~WebGpuGraphicsSystem() override;

  static bool IsAvailable() { return true; }

  std::string name() const override;

 protected:
  void CreateProvider(bool with_presentation) override;

 private:
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override;
};

}  // namespace rex::graphics::webgpu
