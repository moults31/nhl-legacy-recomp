// WASM SDK kernel runtime — provides function dispatch, guest memory,
// and runtime stubs for the Emscripten (WASM) build.
//
// We do NOT compile the SDK's function_dispatcher.cpp — its dependency
// chain pulls in the full Memory/KernelState/Runtime which needs mmap,
// pthread signals, and SEH that WASM doesn't support.
//
// Instead we provide:
//   1. Guest memory dispatch table → REX_LOOKUP_FUNC fast path
//   2. ResolveIndirectFunction → fallback for cross-module calls
//   3. All SDK stubs the generated code & headers need at link time

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <mutex>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <emscripten.h>

#include <rex/ppc.h>
#include <rex/ppc/context.h>
#include <rex/image_info.h>
#include <rex/logging.h>
#include <rex/system/mmio_handler.h>
#include <rex/thread/mutex.h>
#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/perf/counter.h>

// ============================================================================
// Guest memory — dynamically allocated to cover full guest address space.
// Guest base address 0 maps to buffer[0]. Maximum needed: up to the dispatch
// table at IMAGE_BASE + IMAGE_SIZE + code_size*2, i.e. ~2.3 GB.
// ============================================================================

// Code section lives at 0x82450000..0x839E3530
// Dispatch table at 0x82000000 + 0x1EA0000 = 0x83EA0000
// Table size = code_size * 2 + thunk_reserve * 2 ≈ 0x2A9CA60
// Dispatch table is at image_base + image_size = 0x83EA0000
// Table size = code_size * 2 + 0x20000 ≈ 0x2A9CA60
// Max address = 0x83EA0000 + 0x2A9CA60 ≈ 0x8693CA60 (~2.25 GB)
static constexpr size_t kGuestMaxOffset = 0x88000000ull;
static uint8_t* g_guest_base = nullptr;
static std::atomic<bool> g_mem_logged{false};

uint8_t* wasm_guest_base() {
  if (!g_mem_logged.exchange(true)) {
    g_guest_base = (uint8_t*)std::calloc(kGuestMaxOffset, 1);
    if (!g_guest_base) {
      std::fprintf(stderr, "[sdk] FATAL: calloc(%zu) failed\n", (size_t)kGuestMaxOffset);
      std::abort();
    }
    std::fprintf(stderr, "[sdk] guest memory: %.1fGB @ %p\n",
                 (double)kGuestMaxOffset / (1024 * 1024 * 1024), (void*)g_guest_base);
  }
  return g_guest_base;
}

// ============================================================================
// WamFunctionDispatcher
//
// Maps guest addresses → host PPCFunc* via:
//   a) unordered_map for ResolveIndirectFunction
//   b) Guest-memory dispatch table at IMAGE_BASE + IMAGE_SIZE for REX_LOOKUP_FUNC
// ============================================================================

class WamFunctionDispatcher {
 public:
  bool InitFromImage(const rex::PPCImageInfo& info) {
    uint8_t* base = wasm_guest_base();
    code_base_ = static_cast<uint32_t>(info.code_base);
    uint32_t image_base = static_cast<uint32_t>(info.image_base);
    uint32_t image_size = static_cast<uint32_t>(info.image_size);
    uint32_t table_base = image_base + image_size;
    uint32_t table_size = info.code_size * 2 + 0x10000 * 2;

    std::memset(base + table_base, 0, table_size);

    std::fprintf(stderr, "[sdk] dispatch table: guest %08X, %u bytes\n",
                 table_base, table_size);

    int count = 0;
    for (int i = 0; info.func_mappings[i].guest != 0; ++i) {
      uint32_t ga = static_cast<uint32_t>(info.func_mappings[i].guest);
      PPCFunc* host = info.func_mappings[i].host;
      if (!host) continue;

      map_[ga] = host;

      // Write into guest memory for REX_LOOKUP_FUNC fast path.
      uint64_t off = static_cast<uint64_t>(ga - code_base_) * 2;
      uint8_t* slot = base + table_base + off;
      std::memcpy(slot, &host, sizeof(host));
      ++count;
    }

    std::fprintf(stderr, "[sdk] registered %d functions\n", count);
    return true;
  }

  PPCFunc* Get(uint32_t addr) const {
    auto it = map_.find(addr);
    return (it != map_.end()) ? it->second : nullptr;
  }

  static WamFunctionDispatcher* instance() { return s_instance; }
  static void set_instance(WamFunctionDispatcher* p) { s_instance = p; }

 private:
  uint32_t code_base_ = 0;
  std::unordered_map<uint32_t, PPCFunc*> map_;
  static inline WamFunctionDispatcher* s_instance = nullptr;
};

// ============================================================================
// ResolveIndirectFunction — the hook the generated recomp calls on cache miss.
// ============================================================================

namespace rex::runtime {

PPCFunc* ResolveIndirectFunction(uint32_t guest_address) {
  auto* d = WamFunctionDispatcher::instance();
  if (!d) return nullptr;
  PPCFunc* f = d->Get(guest_address);
  if (!f) {
    std::fprintf(stderr, "[sdk] unresolved indirect: 0x%08X\n", guest_address);
  }
  return f;
}

}  // namespace rex::runtime

// ============================================================================
// SDK stubs — only symbols NOT provided inline by SDK headers.
// ============================================================================

// ---- Logging (declared in rex/logging/api.h, not inline) --------------------

namespace rex {
void InitLoggingEarly() {}
void ShutdownLogging() {}
void SetAllLevels(spdlog::level::level_enum) {}
}  // namespace rex

namespace rex::log {
spdlog::logger* GetLoggerRaw(LogCategoryId) { return nullptr; }
}  // namespace rex::log

// ---- Debug (declared in rex/dbg.h, not inline) -----------------------------

namespace rex::debug {
bool IsDebuggerAttached() { return false; }
void Break() {}
namespace detail {
void DebugPrint(const char*) {}
}  // namespace detail
}  // namespace rex::debug

// ---- CVAR (declared in rex/cvar.h, not inline) -----------------------------

namespace rex::cvar {
std::vector<std::string> Init(int, char**) { return {}; }
void ApplyEnvironment() {}
}  // namespace rex::cvar

// ---- Clock (declared in rex/chrono/clock.h, not inline) --------------------

namespace rex::chrono {

uint64_t Clock::host_tick_frequency_platform() { return 1'000'000'000; }
uint64_t Clock::host_tick_count_platform()     { return (uint64_t)(emscripten_get_now() * 1e6); }
uint64_t Clock::QueryHostTickFrequency()       { return host_tick_frequency_platform(); }
uint64_t Clock::QueryHostTickCount()           { return host_tick_count_platform(); }
uint64_t Clock::QueryHostSystemTime()          { return (uint64_t)(emscripten_get_now() * 10000.0); }
uint64_t Clock::QueryHostUptimeMillis()        { return (uint64_t)emscripten_get_now(); }

uint64_t Clock::QueryGuestTickCount()          { return (uint64_t)(emscripten_get_now() * 50000.0); }
uint64_t Clock::QueryGuestSystemTime()         { return (uint64_t)(emscripten_get_now() * 10000.0); }
uint32_t Clock::QueryGuestUptimeMillis()       { return (uint32_t)emscripten_get_now(); }

double   Clock::guest_time_scalar()              { return 1.0; }
void     Clock::set_guest_time_scalar(double)    {}
uint64_t Clock::guest_tick_frequency()           { return 50'000'000; }
void     Clock::set_guest_tick_frequency(uint64_t) {}
uint64_t Clock::guest_system_time_base()         { return 0; }
void     Clock::set_guest_system_time_base(uint64_t) {}

std::pair<uint64_t, uint64_t> Clock::guest_tick_ratio() { return {1, 1}; }

void     Clock::SetGuestSystemTime(uint64_t) {}
uint32_t Clock::ScaleGuestDurationMillis(uint32_t ms)   { return ms; }
int64_t  Clock::ScaleGuestDurationFileTime(int64_t ft)  { return ft; }
void     Clock::ScaleGuestDurationTimeval(int32_t*, int32_t*) {}

}  // namespace rex::chrono

// ---- Threads ---------------------------------------------------------------

namespace rex::thread {

static std::recursive_mutex g_critsec;
std::recursive_mutex& global_critical_region::mutex() { return g_critsec; }
void EnableAffinityConfiguration() {}

}  // namespace rex::thread

// ---- MMIO Handler ----------------------------------------------------------

namespace rex::runtime {
MMIOHandler* MMIOHandler::global_handler() { return nullptr; }
MMIOHandler::~MMIOHandler() = default;
}  // namespace rex::runtime

// ---- SEH — not available on WASM -------------------------------------------

namespace rex {
void initialize_seh() {}
}  // namespace rex


// ============================================================================
// PPCImageConfig — guest image layout (referenced by generated recomp).
// ============================================================================

extern "C" {
const rex::PPCImageInfo PPCImageConfig = {
  /*code_base       =*/ 0x82450000,
  /*code_size       =*/ 0x153E530,
  /*image_base      =*/ 0x82000000,
  /*image_size      =*/ 0x1EA0000,
  /*func_mappings   =*/ PPCFuncMappings,
  /*rexcrt_heap     =*/ false,
  /*register_modules=*/ nullptr,
};
}

// ============================================================================
// Boot — called from main().  Builds the dispatch table, sets up PPC context,
// and calls the guest kernel entry point.
// ============================================================================

extern "C" int wasm_boot_guest() {
  std::fprintf(stderr, "[sdk] NHL Legacy WASM boot (sdk_runtime)\n");

  wasm_guest_base();
  uint8_t* base = g_guest_base;

  auto* disp = new WamFunctionDispatcher();
  WamFunctionDispatcher::set_instance(disp);

  if (!disp->InitFromImage(PPCImageConfig)) {
    std::fprintf(stderr, "[sdk] FATAL: InitFromImage failed\n");
    return 1;
  }

  PPCFunc* entry = disp->Get(static_cast<uint32_t>(PPCImageConfig.code_base));
  if (!entry) {
    std::fprintf(stderr, "[sdk] FATAL: entry 0x%llX not registered\n",
                 (unsigned long long)PPCImageConfig.code_base);
    return 1;
  }

  PPCContext ctx{};
  std::memset(&ctx, 0, sizeof(ctx));
  ctx.r1.u64 = 0x100000000ull - 0x10000ull;
  ctx.r13.u64 = 0x10000000ull;
  ctx.fpscr.InitHost();

  std::fprintf(stderr, "[sdk] calling entry 0x%llX (r1=%llX r13=%llX)\n",
               (unsigned long long)PPCImageConfig.code_base,
               (unsigned long long)ctx.r1.u64,
               (unsigned long long)ctx.r13.u64);

  entry(ctx, base);

  std::fprintf(stderr, "[sdk] guest entry returned r3=0x%llX\n",
               (unsigned long long)ctx.r3.u64);
  return 0;
}
