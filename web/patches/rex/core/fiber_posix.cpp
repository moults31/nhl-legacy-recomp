/**
 * @file        rex/core/fiber_posix.cpp
 * @brief       POSIX backend for rex::thread::Fiber (makecontext/swapcontext)
 *              WASM/Emscripten: no-op stubs (fibers not supported in WASM)
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/platform.h>

// POSIX (Linux/Mac): real ucontext-based fibers
#if (REX_PLATFORM_LINUX || REX_PLATFORM_MAC) && !REX_PLATFORM_EMSCRIPTEN

#include <rex/thread/fiber.h>

#include <cassert>
#include <ucontext.h>

namespace rex::thread {

thread_local Fiber* Fiber::tls_current_ = nullptr;

Fiber* Fiber::ConvertCurrentThread() {
  auto* f = new Fiber();
  if (getcontext(&f->context_) == -1) {
    delete f;
    return nullptr;
  }
  f->is_thread_fiber_ = true;
  tls_current_ = f;
  return f;
}

Fiber* Fiber::Create(size_t stack_size, void (*entry)(void*), void* arg) {
  auto* f = new Fiber();
  f->entry_ = entry;
  f->arg_ = arg;
  f->stack_.resize(stack_size);

  if (getcontext(&f->context_) == -1) {
    delete f;
    return nullptr;
  }
  f->context_.uc_stack.ss_sp = f->stack_.data();
  f->context_.uc_stack.ss_size = f->stack_.size();
  f->context_.uc_link = nullptr;
  makecontext(&f->context_, &Fiber::Trampoline, 0);
  return f;
}

/*static*/ void Fiber::Trampoline() {
  Fiber* f = tls_current_;
  f->entry_(f->arg_);
}

void Fiber::SwitchTo(Fiber* target) {
  Fiber* from = tls_current_;
  tls_current_ = target;
  swapcontext(&from->context_, &target->context_);
}

void Fiber::Destroy() {
  if (is_thread_fiber_) {
    tls_current_ = nullptr;
  } else {
    assert(this != tls_current_ && "Destroy called on the currently running fiber");
  }
  delete this;
}

}  // namespace rex::thread

// WASM/Emscripten: no-op stubs (no ucontext.h, no cooperative fibers)
#elif REX_PLATFORM_EMSCRIPTEN

#include <rex/thread/fiber.h>

namespace rex::thread {

thread_local Fiber* Fiber::tls_current_ = nullptr;

Fiber* Fiber::ConvertCurrentThread() {
  return static_cast<Fiber*>(this);  // return self as marker
}

Fiber* Fiber::Create(size_t stack_size, void (*entry)(void*), void* arg) {
  (void)stack_size; (void)entry; (void)arg;
  // Xbox fibers (CreateFiber) are unused by NHL Legacy (Phase 0b).
  // Return non-null so the guest thinks it succeeded, but the fiber
  // will never be switched to.
  auto* f = new Fiber();
  f->entry_ = entry;
  f->arg_ = arg;
  return f;
}

void Fiber::SwitchTo(Fiber* target) {
  // Not reached — NHL Legacy does not call SwitchToFiber.
  (void)target;
}

void Fiber::Destroy() {
  tls_current_ = nullptr;
  delete this;
}

}  // namespace rex::thread

#endif  // Platform guards
