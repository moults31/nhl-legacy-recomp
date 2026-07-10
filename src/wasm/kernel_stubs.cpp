// Auto-generated kernel import stubs for NHL Legacy WASM port.
// Each __imp__* function: void fn(PPCContext& ctx, uint8_t* base)
// via REX_EXTERN convention. Return values go in ctx.r3.u64.

#include <cstdio>
#include <cstdint>
#include <atomic>

#include <rex/ppc.h>
#include <rex/system/xio.h>
#include <emscripten.h>
#include "vfs_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

// Global call counter to detect infinite loops
static std::atomic<unsigned> g_total_calls{0};
static constexpr unsigned kMaxCalls = 100000;

#define STUB_LOG(name) \
  do { \
    static std::atomic<unsigned> _c{0}; \
    auto n = _c.fetch_add(1, std::memory_order_relaxed); \
    auto total = g_total_calls.fetch_add(1, std::memory_order_relaxed); \
    if (n < 5 || (n % 1000) == 0) \
      std::fprintf(stderr, "[wasm] %-40s #%u (total: %u)\n", name, n + 1, total + 1); \
    if (total > kMaxCalls) { \
      std::fprintf(stderr, "[wasm] ABORT: too many kernel calls (%u)\n", total + 1); \
      std::fflush(stderr); \
      emscripten_force_exit(1); \
    } \
  } while(0)

__attribute__((weak, noinline)) void __imp____C_specific_handler(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp____C_specific_handler");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__DbgBreakPoint(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__DbgBreakPoint");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__DbgPrint(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__DbgPrint");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__ExAllocatePoolTypeWithTag(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__ExAllocatePoolTypeWithTag");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__ExAllocatePoolWithTag(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__ExAllocatePoolWithTag");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__ExCreateThread(PPCContext& ctx, uint8_t* base) {
  // PPC calling convention: r3 = pointer to creation params struct in guest memory
  // The start_address is at offset 0x150 in the guest KTHREAD/CreationParams.
  // Read it and call the start function directly — this is how the kernel boots the game.
  uint32_t params_ptr = (uint32_t)ctx.r3.u32;
  // Try the expected offset for start_address in X_KTHREAD (0x150)
  uint32_t start_addr = 0;
  uint32_t xapi_startup = 0;
  if (params_ptr >= 0x1000) {
    start_addr = __builtin_bswap32(*(volatile uint32_t*)(base + params_ptr + 0x150));
    xapi_startup = __builtin_bswap32(*(volatile uint32_t*)(base + params_ptr + 0x14C));
  }
  std::fprintf(stderr, "[wasm] ExCreateThread(params=0x%08X, start=0x%08X, xapi=0x%08X)\n",
               params_ptr, start_addr, xapi_startup);

  if (xapi_startup) {
    // Call the Xapi thread startup trampoline, passing start_addr as argument
    auto* fn = rex::runtime::ResolveIndirectFunction(xapi_startup);
    if (fn) {
      PPCContext tctx{};
      std::memset(&tctx, 0, sizeof(tctx));
      tctx.r1.u64 = 0x40000000ull;
      tctx.r13.u64 = 0x10000000ull;
      tctx.r3.u64 = start_addr;
      tctx.fpscr.InitHost();
      std::fprintf(stderr, "[wasm] → calling xapi_startup at 0x%08X with start=0x%08X\n",
                   xapi_startup, start_addr);
      fn(tctx, base);
      ctx.r3.u64 = 0;
    } else {
      ctx.r3.u64 = 0xC0000001; // STATUS_UNSUCCESSFUL
    }
  } else if (start_addr) {
    // Direct thread start — call the start function
    auto* fn = rex::runtime::ResolveIndirectFunction(start_addr);
    if (fn) {
      PPCContext tctx{};
      std::memset(&tctx, 0, sizeof(tctx));
      tctx.r1.u64 = 0x40000000ull;
      tctx.r13.u64 = 0x10000000ull;
      tctx.fpscr.InitHost();
      std::fprintf(stderr, "[wasm] → calling thread start at 0x%08X\n", start_addr);
      fn(tctx, base);
      ctx.r3.u64 = 0;
    } else {
      ctx.r3.u64 = 0xC0000001;
    }
  } else {
    STUB_LOG("__imp__ExCreateThread");
    ctx.r3.u64 = 0u;
  }
}
__attribute__((weak, noinline)) void __imp__ExFreePool(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__ExFreePool");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__ExGetXConfigSetting(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__ExGetXConfigSetting");
  ctx.r3.u64 = 1u;
}
__attribute__((weak, noinline)) void __imp__ExRegisterTitleTerminateNotification(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__ExRegisterTitleTerminateNotification");
  ctx.r3.u64 = 1u;
}
__attribute__((weak, noinline)) void __imp__ExTerminateThread(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__ExTerminateThread");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__HalReturnToFirmware(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__HalReturnToFirmware");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__IoCheckShareAccess(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__IoCheckShareAccess");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__IoCompleteRequest(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__IoCompleteRequest");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__IoCreateDevice(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__IoCreateDevice");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__IoDeleteDevice(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__IoDeleteDevice");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__IoDismountVolume(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__IoDismountVolume");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__IoDismountVolumeByFileHandle(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__IoDismountVolumeByFileHandle");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__IoInvalidDeviceRequest(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__IoInvalidDeviceRequest");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__IoRemoveShareAccess(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__IoRemoveShareAccess");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__IoSetShareAccess(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__IoSetShareAccess");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeAcquireSpinLockAtRaisedIrql(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeAcquireSpinLockAtRaisedIrql");
  ctx.r3.u64 = 1u;
}
__attribute__((weak, noinline)) void __imp__KeBugCheck(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeBugCheck");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeBugCheckEx(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeBugCheckEx");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeDelayExecutionThread(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeDelayExecutionThread");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeEnableFpuExceptions(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeEnableFpuExceptions");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeEnterCriticalRegion(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeEnterCriticalRegion");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeGetCurrentProcessType(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeGetCurrentProcessType");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeInitializeApc(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeInitializeApc");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeInitializeDpc(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeInitializeDpc");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeInsertQueueApc(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeInsertQueueApc");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeInsertQueueDpc(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeInsertQueueDpc");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeLeaveCriticalRegion(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeLeaveCriticalRegion");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeLockL2(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeLockL2");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeQueryBasePriorityThread(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeQueryBasePriorityThread");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeQueryPerformanceFrequency(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeQueryPerformanceFrequency");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeQuerySystemTime(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeQuerySystemTime");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeReleaseSpinLockFromRaisedIrql(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeReleaseSpinLockFromRaisedIrql");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeResetEvent(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeResetEvent");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeSetAffinityThread(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeSetAffinityThread");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeSetBasePriorityThread(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeSetBasePriorityThread");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeSetCurrentProcessType(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeSetCurrentProcessType");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeSetEvent(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeSetEvent");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeTlsAlloc(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeTlsAlloc");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeTlsFree(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeTlsFree");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeTlsGetValue(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeTlsGetValue");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeTlsSetValue(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeTlsSetValue");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeUnlockL2(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeUnlockL2");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeWaitForMultipleObjects(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeWaitForMultipleObjects");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KeWaitForSingleObject(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KeWaitForSingleObject");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KfAcquireSpinLock(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KfAcquireSpinLock");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KfReleaseSpinLock(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KfReleaseSpinLock");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__KiApcNormalRoutineNop(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__KiApcNormalRoutineNop");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__MmAllocatePhysicalMemoryEx(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__MmAllocatePhysicalMemoryEx");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__MmFreePhysicalMemory(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__MmFreePhysicalMemory");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__MmGetPhysicalAddress(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__MmGetPhysicalAddress");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__MmMapIoSpace(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__MmMapIoSpace");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__MmQueryAddressProtect(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__MmQueryAddressProtect");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__MmQueryStatistics(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__MmQueryStatistics");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__MmSetAddressProtect(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__MmSetAddressProtect");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_accept(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_accept");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_bind(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_bind");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_closesocket(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_closesocket");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_connect(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_connect");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_getpeername(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_getpeername");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_getsockname(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_getsockname");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_getsockopt(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_getsockopt");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_inet_addr(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_inet_addr");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_ioctlsocket(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_ioctlsocket");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_recv(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_recv");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_recvfrom(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_recvfrom");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_select(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_select");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_send(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_send");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_sendto(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_sendto");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_setsockopt(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_setsockopt");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_shutdown(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_shutdown");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_socket(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_socket");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_WSACleanup(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_WSACleanup");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_WSACloseEvent(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_WSACloseEvent");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_WSACreateEvent(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_WSACreateEvent");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_WSAGetLastError(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_WSAGetLastError");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_WSAGetOverlappedResult(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_WSAGetOverlappedResult");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_WSARecv(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_WSARecv");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_WSARecvFrom(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_WSARecvFrom");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_WSAResetEvent(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_WSAResetEvent");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_WSASetEvent(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_WSASetEvent");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_WSAStartup(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_WSAStartup");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_WSAWaitForMultipleEvents(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_WSAWaitForMultipleEvents");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XNetCleanup(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XNetCleanup");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XNetConnect(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XNetConnect");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XNetCreateKey(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XNetCreateKey");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XNetDnsLookup(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XNetDnsLookup");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XNetDnsRelease(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XNetDnsRelease");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XNetGetConnectStatus(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XNetGetConnectStatus");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XNetGetEthernetLinkStatus(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XNetGetEthernetLinkStatus");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XNetGetTitleXnAddr(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XNetGetTitleXnAddr");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XNetQosListen(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XNetQosListen");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XNetQosLookup(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XNetQosLookup");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XNetQosRelease(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XNetQosRelease");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XNetQosServiceLookup(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XNetQosServiceLookup");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XNetRandom(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XNetRandom");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XNetRegisterKey(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XNetRegisterKey");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XNetServerToInAddr(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XNetServerToInAddr");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XNetStartup(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XNetStartup");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XNetUnregisterInAddr(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XNetUnregisterInAddr");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XNetUnregisterKey(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XNetUnregisterKey");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XNetXnAddrToInAddr(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XNetXnAddrToInAddr");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NetDll_XnpLogonGetStatus(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NetDll_XnpLogonGetStatus");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtAllocateVirtualMemory(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtAllocateVirtualMemory");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtCancelTimer(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtCancelTimer");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtClearEvent(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtClearEvent");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtClose(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtClose");
  uint32_t handle = (uint32_t)ctx.r3.u32;
  WasmCloseFile(handle);
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtCreateEvent(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtCreateEvent");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtCreateFile(PPCContext& ctx, uint8_t* base) {
  STUB_LOG("__imp__NtCreateFile");
  // r3 = PHANDLE, r5 = X_OBJECT_ATTRIBUTES (ANSI name works on X360)
  // X_OBJECT_ATTRIBUTES: +0 root_dir, +4 name_ptr(ANSI_STRING*)
  // ANSI_STRING: +0 length(be16), +2 max_len(be16), +4 pointer(be32)
  // length of 0xFFFF means null-terminated (length unknown)
  uint32_t handle_ptr = ctx.r3.u32;
  uint32_t obj_attr = ctx.r5.u32;
  if (obj_attr >= 0x1000) {
    uint32_t ansi_ptr = __builtin_bswap32(*(volatile uint32_t*)(base + obj_attr + 4));
    if (ansi_ptr >= 0x1000) {
      uint32_t buf = __builtin_bswap32(*(volatile uint32_t*)(base + ansi_ptr + 4));
      uint16_t len = __builtin_bswap16(*(volatile uint16_t*)(base + ansi_ptr));
      if (buf >= 0x1000) { std::fprintf(stderr, "[vfs] path len=%u\n", len);
        // Dump first 20 bytes of buffer to see if it's ANSI or UTF-16
        for (int di = 0; di < 20; ++di)
          std::fprintf(stderr, "%02X", *(volatile uint8_t*)(base + buf + di));
        std::fprintf(stderr, "\n");
        char path[260];
        int plen = 0;
        // Read up to 259 chars or until null terminator
        for (int i = 0; i < 259 && i < (int)(len == 0xFFFF ? 259 : len); ++i) {
          char c = (char)(*(volatile uint8_t*)(base + buf + i));
          if (c == '\0') break;
          if (c >= 0x20) path[plen++] = c;
        }
        path[plen] = '\0';
        const char* fp = path;
        const char* dp = "\\Device\\Harddisk0\\Partition1";
        if (strncmp(path, dp, strlen(dp)) == 0) {
          fp = path + strlen(dp);
          while (*fp == '\\') ++fp;
        }
        uint32_t st;
        uint32_t h = WasmOpenFile(fp, st);
        if (h && handle_ptr >= 0x1000)
          *(volatile uint32_t*)(base + handle_ptr) = __builtin_bswap32(h);
        ctx.r3.u64 = st;
        return;
      }
    }
  }
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtCreateMutant(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtCreateMutant");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtCreateSemaphore(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtCreateSemaphore");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtCreateTimer(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtCreateTimer");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtDeviceIoControlFile(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtDeviceIoControlFile");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtDuplicateObject(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtDuplicateObject");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtFlushBuffersFile(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtFlushBuffersFile");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtFreeVirtualMemory(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtFreeVirtualMemory");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtOpenFile(PPCContext& ctx, uint8_t* base) {
  STUB_LOG("__imp__NtOpenFile");
  uint32_t handle_ptr = ctx.r3.u32;
  uint32_t obj_attr = ctx.r5.u32;
  if (obj_attr >= 0x1000) {
    uint32_t ansi_ptr = __builtin_bswap32(*(volatile uint32_t*)(base + obj_attr + 4));
    if (ansi_ptr >= 0x1000) {
      uint32_t buf = __builtin_bswap32(*(volatile uint32_t*)(base + ansi_ptr + 4));
      uint16_t len = __builtin_bswap16(*(volatile uint16_t*)(base + ansi_ptr));
      if (buf >= 0x1000) { std::fprintf(stderr, "[vfs] path len=%u\n", len);
        // Dump first 20 bytes of buffer to see if it's ANSI or UTF-16
        for (int di = 0; di < 20; ++di)
          std::fprintf(stderr, "%02X", *(volatile uint8_t*)(base + buf + di));
        std::fprintf(stderr, "\n");
        char path[260];
        int plen = 0;
        for (int i = 0; i < 259 && i < (int)(len == 0xFFFF ? 259 : len); ++i) {
          char c = (char)(*(volatile uint8_t*)(base + buf + i));
          if (c == '\0') break;
          if (c >= 0x20) path[plen++] = c;
        }
        path[plen] = '\0';
        const char* fp = path;
        const char* dp = "\\Device\\Harddisk0\\Partition1";
        if (strncmp(path, dp, strlen(dp)) == 0) {
          fp = path + strlen(dp);
          while (*fp == '\\') ++fp;
        }
        uint32_t st;
        uint32_t h = WasmOpenFile(fp, st);
        if (h && handle_ptr >= 0x1000)
          *(volatile uint32_t*)(base + handle_ptr) = __builtin_bswap32(h);
        ctx.r3.u64 = st;
        return;
      }
    }
  }
  ctx.r3.u64 = 0xC0000034u;
}
__attribute__((weak, noinline)) void __imp__NtQueryDirectoryFile(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtQueryDirectoryFile");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtQueryFullAttributesFile(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtQueryFullAttributesFile");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtQueryInformationFile(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtQueryInformationFile");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtQueryVirtualMemory(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtQueryVirtualMemory");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtQueryVolumeInformationFile(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtQueryVolumeInformationFile");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtReadFile(PPCContext& ctx, uint8_t* base) {
  STUB_LOG("__imp__NtReadFile");
  // r3 = FileHandle, r8 = Buffer (guest addr), r9 = Length, r7 = IoStatusBlock
  uint32_t handle = ctx.r3.u32;
  uint32_t buf_addr = ctx.r8.u32;
  uint32_t length = ctx.r9.u32;

  if (handle && buf_addr >= 0x1000 && length > 0) {
    uint32_t bytes_read = WasmReadFile(handle, base + buf_addr, length);
    // Write bytes_read to IoStatusBlock+0 (or just return success)
    ctx.r3.u64 = 0u;  // STATUS_SUCCESS

    // If IoStatusBlock is provided, write the info
    uint32_t iosb = ctx.r7.u32;
    if (iosb >= 0x1000) {
      *(volatile uint32_t*)(base + iosb) = 0;                          // Status
      *(volatile uint32_t*)(base + iosb + 4) = __builtin_bswap32(bytes_read);  // Information
    }
    return;
  }

  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtReadFileScatter(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtReadFileScatter");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtReleaseMutant(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtReleaseMutant");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtReleaseSemaphore(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtReleaseSemaphore");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtResumeThread(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtResumeThread");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtSetEvent(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtSetEvent");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtSetInformationFile(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtSetInformationFile");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtSetTimerEx(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtSetTimerEx");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtSuspendThread(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtSuspendThread");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtWaitForSingleObjectEx(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtWaitForSingleObjectEx");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtWriteFile(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtWriteFile");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtWriteFileGather(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtWriteFileGather");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__NtYieldExecution(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__NtYieldExecution");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__ObCreateSymbolicLink(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__ObCreateSymbolicLink");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__ObDeleteSymbolicLink(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__ObDeleteSymbolicLink");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__ObDereferenceObject(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__ObDereferenceObject");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__ObIsTitleObject(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__ObIsTitleObject");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__ObReferenceObject(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__ObReferenceObject");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__ObReferenceObjectByHandle(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__ObReferenceObjectByHandle");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__RtlCaptureContext(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__RtlCaptureContext");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__RtlCompareMemoryUlong(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__RtlCompareMemoryUlong");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__RtlCompareStringN(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__RtlCompareStringN");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__RtlEnterCriticalSection(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__RtlEnterCriticalSection");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__RtlFillMemoryUlong(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__RtlFillMemoryUlong");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__RtlImageXexHeaderField(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__RtlImageXexHeaderField");
  ctx.r3.u64 = 0x100u;
}
__attribute__((weak, noinline)) void __imp__RtlInitAnsiString(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__RtlInitAnsiString");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__RtlInitializeCriticalSection(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__RtlInitializeCriticalSection");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__RtlInitializeCriticalSectionAndSpinCount(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__RtlInitializeCriticalSectionAndSpinCount");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__RtlLeaveCriticalSection(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__RtlLeaveCriticalSection");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__RtlMultiByteToUnicodeN(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__RtlMultiByteToUnicodeN");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__RtlNtStatusToDosError(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__RtlNtStatusToDosError");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__RtlRaiseException(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__RtlRaiseException");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__RtlTimeFieldsToTime(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__RtlTimeFieldsToTime");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__RtlTimeToTimeFields(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__RtlTimeToTimeFields");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__RtlTryEnterCriticalSection(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__RtlTryEnterCriticalSection");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__RtlUnicodeToMultiByteN(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__RtlUnicodeToMultiByteN");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__RtlUnwind(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__RtlUnwind");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__RtlUpcaseUnicodeChar(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__RtlUpcaseUnicodeChar");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp___snprintf(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp___snprintf");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__sprintf(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__sprintf");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__StfsControlDevice(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__StfsControlDevice");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__StfsCreateDevice(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__StfsCreateDevice");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdCallGraphicsNotificationRoutines(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdCallGraphicsNotificationRoutines");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdEnableDisableClockGating(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdEnableDisableClockGating");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdEnableRingBufferRPtrWriteBack(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdEnableRingBufferRPtrWriteBack");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdGetCurrentDisplayGamma(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdGetCurrentDisplayGamma");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdGetCurrentDisplayInformation(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdGetCurrentDisplayInformation");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdGetSystemCommandBuffer(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdGetSystemCommandBuffer");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdInitializeEngines(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdInitializeEngines");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdInitializeRingBuffer(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdInitializeRingBuffer");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdInitializeScalerCommandBuffer(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdInitializeScalerCommandBuffer");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdIsHSIOTrainingSucceeded(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdIsHSIOTrainingSucceeded");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdPersistDisplay(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdPersistDisplay");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdQueryVideoFlags(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdQueryVideoFlags");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdQueryVideoMode(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdQueryVideoMode");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdRetrainEDRAM(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdRetrainEDRAM");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdRetrainEDRAMWorker(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdRetrainEDRAMWorker");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdSetDisplayMode(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdSetDisplayMode");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdSetDisplayModeOverride(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdSetDisplayModeOverride");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdSetGraphicsInterruptCallback(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdSetGraphicsInterruptCallback");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdSetSystemCommandBufferGpuIdentifierAddress(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdSetSystemCommandBufferGpuIdentifierAddress");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdShutdownEngines(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdShutdownEngines");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__VdSwap(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__VdSwap");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp___vsnprintf(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp___vsnprintf");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamAlloc(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamAlloc");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamBackgroundDownloadSetMode(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamBackgroundDownloadSetMode");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamContentClose(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamContentClose");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamContentCreateEnumerator(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamContentCreateEnumerator");
  ctx.r3.u64 = 0x80070103u;
}
__attribute__((weak, noinline)) void __imp__XamContentCreateEx(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamContentCreateEx");
  ctx.r3.u64 = 0x80070103u;
}
__attribute__((weak, noinline)) void __imp__XamContentGetCreator(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamContentGetCreator");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamContentGetDeviceData(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamContentGetDeviceData");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamContentGetDeviceState(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamContentGetDeviceState");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamContentGetLicenseMask(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamContentGetLicenseMask");
  ctx.r3.u64 = 0x80070002u;
}
__attribute__((weak, noinline)) void __imp__XamCreateEnumeratorHandle(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamCreateEnumeratorHandle");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamEnumerate(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamEnumerate");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamFree(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamFree");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamGetExecutionId(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamGetExecutionId");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamGetLanguage(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamGetLanguage");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamGetLocaleEx(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamGetLocaleEx");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamGetOverlappedResult(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamGetOverlappedResult");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamGetPrivateEnumStructureFromHandle(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamGetPrivateEnumStructureFromHandle");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamGetSystemVersion(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamGetSystemVersion");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamInputGetCapabilities(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamInputGetCapabilities");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamInputGetKeystrokeEx(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamInputGetKeystrokeEx");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamInputGetState(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamInputGetState");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamInputSetState(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamInputSetState");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamLoaderLaunchTitle(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamLoaderLaunchTitle");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamLoaderTerminateTitle(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamLoaderTerminateTitle");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamNotifyCreateListener(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamNotifyCreateListener");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamSessionCreateHandle(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamSessionCreateHandle");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamSessionRefObjByHandle(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamSessionRefObjByHandle");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamShowCustomPlayerListUI(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamShowCustomPlayerListUI");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamShowDeviceSelectorUI(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamShowDeviceSelectorUI");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamShowDirtyDiscErrorUI(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamShowDirtyDiscErrorUI");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamShowFriendsUI(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamShowFriendsUI");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamShowGameInviteUI(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamShowGameInviteUI");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamShowGamerCardUIForXUID(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamShowGamerCardUIForXUID");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamShowKeyboardUI(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamShowKeyboardUI");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamShowMarketplaceDownloadItemsUI(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamShowMarketplaceDownloadItemsUI");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamShowMarketplaceUI(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamShowMarketplaceUI");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamShowMessageBoxUI(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamShowMessageBoxUI");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamShowMessageBoxUIEx(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamShowMessageBoxUIEx");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamShowMessageComposeUI(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamShowMessageComposeUI");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamShowMessagesUI(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamShowMessagesUI");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamShowPlayerReviewUI(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamShowPlayerReviewUI");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamShowSigninUI(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamShowSigninUI");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamTaskCloseHandle(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamTaskCloseHandle");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamTaskSchedule(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamTaskSchedule");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamTaskShouldExit(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamTaskShouldExit");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamUserAreUsersFriends(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamUserAreUsersFriends");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamUserCheckPrivilege(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamUserCheckPrivilege");
  ctx.r3.u64 = 1u;
}
__attribute__((weak, noinline)) void __imp__XamUserGetDeviceContext(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamUserGetDeviceContext");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamUserGetMembershipTierFromXUID(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamUserGetMembershipTierFromXUID");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamUserGetName(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamUserGetName");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamUserGetOnlineCountryFromXUID(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamUserGetOnlineCountryFromXUID");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamUserGetSigninInfo(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamUserGetSigninInfo");
  ctx.r3.u64 = 1u;
}
__attribute__((weak, noinline)) void __imp__XamUserGetSigninState(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamUserGetSigninState");
  ctx.r3.u64 = 1u;
}
__attribute__((weak, noinline)) void __imp__XamUserGetXUID(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamUserGetXUID");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamUserReadProfileSettings(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamUserReadProfileSettings");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamVoiceClose(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamVoiceClose");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamVoiceCreate(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamVoiceCreate");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamVoiceHeadsetPresent(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamVoiceHeadsetPresent");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamVoiceIsActiveProcess(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamVoiceIsActiveProcess");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XamVoiceSubmitPacket(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XamVoiceSubmitPacket");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XAudioEnableDucker(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XAudioEnableDucker");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XAudioGetDuckerAttackTime(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XAudioGetDuckerAttackTime");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XAudioGetDuckerHoldTime(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XAudioGetDuckerHoldTime");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XAudioGetDuckerLevel(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XAudioGetDuckerLevel");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XAudioGetDuckerReleaseTime(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XAudioGetDuckerReleaseTime");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XAudioGetDuckerThreshold(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XAudioGetDuckerThreshold");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XAudioGetSpeakerConfig(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XAudioGetSpeakerConfig");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XAudioGetVoiceCategoryVolume(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XAudioGetVoiceCategoryVolume");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XAudioRegisterRenderDriverClient(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XAudioRegisterRenderDriverClient");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XAudioSubmitRenderDriverFrame(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XAudioSubmitRenderDriverFrame");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XAudioUnregisterRenderDriverClient(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XAudioUnregisterRenderDriverClient");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XeCryptSha(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XeCryptSha");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XeCryptSha256Final(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XeCryptSha256Final");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XeCryptSha256Init(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XeCryptSha256Init");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XeCryptSha256Update(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XeCryptSha256Update");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XeCryptSha384Final(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XeCryptSha384Final");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XeCryptSha384Init(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XeCryptSha384Init");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XeCryptSha384Update(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XeCryptSha384Update");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XeCryptSha512Final(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XeCryptSha512Final");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XeCryptSha512Init(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XeCryptSha512Init");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XeCryptSha512Update(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XeCryptSha512Update");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XeCryptShaFinal(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XeCryptShaFinal");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XeCryptShaInit(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XeCryptShaInit");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XeCryptShaUpdate(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XeCryptShaUpdate");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XeKeysConsolePrivateKeySign(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XeKeysConsolePrivateKeySign");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XeKeysConsoleSignatureVerification(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XeKeysConsoleSignatureVerification");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XexCheckExecutablePrivilege(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XexCheckExecutablePrivilege");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XexGetModuleHandle(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XexGetModuleHandle");
  ctx.r3.u64 = 1u;
}
__attribute__((weak, noinline)) void __imp__XexGetProcedureAddress(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XexGetProcedureAddress");
  ctx.r3.u64 = 1u;
}
__attribute__((weak, noinline)) void __imp__XexLoadImage(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XexLoadImage");
  ctx.r3.u64 = 1u;
}
__attribute__((weak, noinline)) void __imp__XexUnloadImage(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XexUnloadImage");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XGetAVPack(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XGetAVPack");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XGetGameRegion(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XGetGameRegion");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XGetVideoMode(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XGetVideoMode");
  ctx.r3.u64 = 0u;
}
static std::atomic<unsigned> xma_context_id{1};
__attribute__((weak, noinline)) void __imp__XMACreateContext(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMACreateContext");
  ctx.r3.u64 = xma_context_id.fetch_add(1, std::memory_order_relaxed);
}
__attribute__((weak, noinline)) void __imp__XMADisableContext(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMADisableContext");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMAEnableContext(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMAEnableContext");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMAGetOutputBufferReadOffset(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMAGetOutputBufferReadOffset");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMAGetOutputBufferWriteOffset(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMAGetOutputBufferWriteOffset");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMAInitializeContext(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMAInitializeContext");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMAIsInputBuffer0Valid(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMAIsInputBuffer0Valid");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMAIsInputBuffer1Valid(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMAIsInputBuffer1Valid");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMAIsOutputBufferValid(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMAIsOutputBufferValid");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMAReleaseContext(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMAReleaseContext");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMASetInputBuffer0(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMASetInputBuffer0");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMASetInputBuffer0Valid(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMASetInputBuffer0Valid");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMASetInputBuffer1(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMASetInputBuffer1");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMASetInputBuffer1Valid(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMASetInputBuffer1Valid");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMASetInputBufferReadOffset(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMASetInputBufferReadOffset");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMASetOutputBufferReadOffset(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMASetOutputBufferReadOffset");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMASetOutputBufferValid(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMASetOutputBufferValid");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMsgCancelIORequest(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMsgCancelIORequest");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMsgCompleteIORequest(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMsgCompleteIORequest");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMsgInProcessCall(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMsgInProcessCall");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMsgStartIORequest(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMsgStartIORequest");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XMsgStartIORequestEx(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XMsgStartIORequestEx");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XNetLogonGetMachineID(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XNetLogonGetMachineID");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XNetLogonGetTitleID(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XNetLogonGetTitleID");
  ctx.r3.u64 = 0xC0DE0000u;
}
__attribute__((weak, noinline)) void __imp__XNotifyGetNext(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XNotifyGetNext");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XUsbcamGetState(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XUsbcamGetState");
  ctx.r3.u64 = 0u;
}
__attribute__((weak, noinline)) void __imp__XUsbcamSetConfig(PPCContext& ctx, uint8_t* base) {
  (void)base;
  STUB_LOG("__imp__XUsbcamSetConfig");
  ctx.r3.u64 = 0u;
}

#ifdef __cplusplus
}
#endif
