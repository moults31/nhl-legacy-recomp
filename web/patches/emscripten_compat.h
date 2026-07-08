// Emscripten compat shims for missing Linux APIs used by the SDK's
// threading_posix.cpp and other core files.
//
// Threading:
//   syscall(SYS_gettid) → gettid() via pthread_self
//   pthread_sigqueue      → stub (no real-time signals in WASM)
//   std::chrono::clock_cast → conditionally disabled
//
// Memory:
//   madvise(MADV_DONTNEED) → no-op (no page-level memory management)
//   mmap(MAP_FIXED)        → reimplemented for WASM linear memory
//
// Filesystem:
//   realpath / readlink    → identity (no symlinks in WASM VFS)
//
#pragma once

#include <cstdint>
#include <cerrno>
#include <sys/types.h>

// =====================================================
// Threading compat
// =====================================================

#include <pthread.h>
#include <unistd.h>

// syscall(SYS_gettid) → gettid() emulation
#include <sys/syscall.h>
#ifndef SYS_gettid
#define SYS_gettid 0  // unused sentinel
#endif

static inline pid_t wasm_gettid() {
  return (pid_t)(intptr_t)pthread_self();
}

#ifndef __EMSCRIPTEN__
#error "This header is for Emscripten/WASM builds only"
#endif

// Override syscall for the SDK's tid query
#define syscall(nr, ...) (wasm_gettid())  // ignores nr, always returns tid

// pthread_sigqueue → not supported in WASM; stub it
#include <signal.h>
static inline int wasm_pthread_sigqueue(pthread_t thread, int sig, const union sigval) {
  (void)thread; (void)sig;
  errno = ENOSYS;
  return -1;
}
#define pthread_sigqueue(thread, sig, value) wasm_pthread_sigqueue(thread, sig, value)

// clock_cast support — libc++ on emscripten may lack some C++20 chrono features
// (handled inline in the chrono headers via #if __cpp_lib_chrono >= 201907L)

// =====================================================
// Memory compat
// =====================================================

// madvise: not supported in WASM (no page-level VM management)
#include <sys/mman.h>
#ifndef MADV_DONTNEED
#define MADV_DONTNEED 4
#endif

// Override madvise → no-op
static inline int wasm_madvise(void*, size_t, int) { return 0; }
#define madvise(addr, len, advice) wasm_madvise(addr, len, advice)
