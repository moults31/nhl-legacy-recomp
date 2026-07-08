#include <rex/assert.h>

#include "windowed_app_context_html5.h"

#include <emscripten.h>

namespace rex {
namespace ui {

HTML5WindowedAppContext::~HTML5WindowedAppContext() = default;

void HTML5WindowedAppContext::NotifyUILoopOfPendingFunctions() {
  std::lock_guard<std::mutex> lock(pending_functions_async_pending_mutex_);
  if (!pending_functions_async_pending_) {
    pending_functions_async_pending_ = true;
    emscripten_async_call(PendingFunctionsWorker, this, 0);
  }
}

void HTML5WindowedAppContext::PlatformQuitFromUIThread() {
  if (quit_async_pending_ || !main_loop_running_) {
    return;
  }
  quit_async_pending_ = true;
  emscripten_cancel_main_loop();
}

void HTML5WindowedAppContext::PendingFunctionsWorker(void* data) {
  auto ctx = static_cast<HTML5WindowedAppContext*>(data);
  {
    std::lock_guard<std::mutex> lock(ctx->pending_functions_async_pending_mutex_);
    ctx->pending_functions_async_pending_ = false;
  }
  ctx->ExecutePendingFunctionsFromUIThread();
}

void HTML5WindowedAppContext::MainLoopWorker(void* data) {
  auto ctx = static_cast<HTML5WindowedAppContext*>(data);
  ctx->ExecutePendingFunctionsFromUIThread();
}

void HTML5WindowedAppContext::RunMainHTMLLoop() {
  if (HasQuitFromUIThread()) {
    return;
  }
  main_loop_running_ = true;
  emscripten_set_main_loop_arg(MainLoopWorker, this, 0, 1);
  main_loop_running_ = false;

  if (quit_async_pending_) {
    quit_async_pending_ = false;
  }
  QuitFromUIThread();
}

}  // namespace ui
}  // namespace rex
