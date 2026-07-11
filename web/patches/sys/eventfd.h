// Emscripten compat: sys/eventfd.h
// WASM/Emscripten does not provide eventfd. Provide minimal pthreads-backed fallback.

#pragma once

#include <cerrno>
#include <pthread.h>
#include <cstdint>

// Emscripten also lacks syscall definitions used by threading_posix.cpp
#ifndef SYS_gettid
#define SYS_gettid 186
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define EFD_CLOEXEC 02000000
#define EFD_NONBLOCK 04000
#define EFD_SEMAPHORE 1

typedef uint64_t eventfd_t;

struct efd_wrapper {
  int value;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
};

static inline int eventfd(unsigned int initval, int flags) {
  (void)flags;
  auto* efd = new efd_wrapper();
  efd->value = (int)initval;
  pthread_mutex_init(&efd->mutex, nullptr);
  pthread_cond_init(&efd->cond, nullptr);
  return (int)(intptr_t)efd;
}

static inline int eventfd_write(int fd, eventfd_t value) {
  auto* efd = (efd_wrapper*)(intptr_t)fd;
  if (!efd) { errno = EINVAL; return -1; }
  pthread_mutex_lock(&efd->mutex);
  efd->value += (int)value;
  pthread_cond_broadcast(&efd->cond);
  pthread_mutex_unlock(&efd->mutex);
  return 0;
}

static inline int eventfd_read(int fd, eventfd_t* value) {
  auto* efd = (efd_wrapper*)(intptr_t)fd;
  if (!efd) { errno = EINVAL; return -1; }
  pthread_mutex_lock(&efd->mutex);
  while (efd->value == 0) {
    pthread_cond_wait(&efd->cond, &efd->mutex);
  }
  *value = (eventfd_t)efd->value;
  efd->value = 0;
  pthread_mutex_unlock(&efd->mutex);
  return 0;
}

static inline int close_eventfd(int fd) {
  auto* efd = (efd_wrapper*)(intptr_t)fd;
  if (!efd) { errno = EINVAL; return -1; }
  pthread_mutex_destroy(&efd->mutex);
  pthread_cond_destroy(&efd->cond);
  delete efd;
  return 0;
}

#ifdef __cplusplus
}
#endif
