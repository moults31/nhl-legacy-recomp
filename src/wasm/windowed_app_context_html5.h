#pragma once

#include <mutex>

#include <rex/ui/windowed_app_context.h>

namespace rex {
namespace ui {

class HTML5WindowedAppContext final : public WindowedAppContext {
 public:
  HTML5WindowedAppContext() = default;
  ~HTML5WindowedAppContext() override;

  void RunMainHTMLLoop();

 private:
  void NotifyUILoopOfPendingFunctions() override;
  void PlatformQuitFromUIThread() override;

  static void PendingFunctionsWorker(void* data);
  static void MainLoopWorker(void* data);

  std::mutex pending_functions_async_pending_mutex_;
  bool pending_functions_async_pending_ = false;

  bool main_loop_running_ = false;
  bool quit_async_pending_ = false;
};

}  // namespace ui
}  // namespace rex
