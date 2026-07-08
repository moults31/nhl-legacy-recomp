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
#include <chrono>
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

// Timeout handler
// Note: WASM is single-threaded and synchronous. If the guest enters a tight
// computational loop, we cannot break out from the outside. The only solution
// is to run the guest in a web worker with a timeout, or fix the kernel stubs
// to not cause infinite loops. For now, we document this as a known limitation.
static double g_start_time = 0;
static bool g_timeout_reached = false;

static void timeout_check() {
  if (g_start_time > 0) {
    double elapsed = emscripten_get_now() - g_start_time;
    if (elapsed > 10000.0) {  // 10 seconds
      std::fprintf(stderr, "[sdk] TIMEOUT: guest execution exceeded 10 seconds\n");
      std::fflush(stderr);
      g_timeout_reached = true;
      // Cannot force exit from WASM — documented limitation
    }
  }
}

// ============================================================================
// Guest memory — dynamically allocated to cover full guest address space.
// Guest base address 0 maps to buffer[0]. Maximum needed: up to the dispatch
// table at IMAGE_BASE + IMAGE_SIZE + code_size*2, i.e. ~2.3 GB.
// ============================================================================

// Code section lives at 0x82450000..0x839E3530
// Dispatch table at 0x82000000 + 0x1EA0000 = 0x83EA0000
// Table size = code_size * 2 + thunk_reserve * 2 ≈ 0x2A9CA60
// 3 GB covers most of the 4 GB space. OOB beyond this is rare.
static constexpr size_t kGuestMaxOffset = 0xD0000000ull; // ~3.25 GB
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

static void __wasm_trap_handler(PPCContext& ctx, uint8_t* base) {
  (void)base;
  static std::atomic<unsigned> _cnt{0};
  auto n = _cnt.fetch_add(1, std::memory_order_relaxed);
  if (n < 5) std::fprintf(stderr, "[sdk] TRAP (#%u) → 0 (skip)\n", n + 1);
  ctx.r3.u64 = 0u;  // return 0 = success/null — skips loops
}

PPCFunc* ResolveIndirectFunction(uint32_t guest_address) {
  auto* d = WamFunctionDispatcher::instance();
  if (!d) return &__wasm_trap_handler;
  PPCFunc* f = d->Get(guest_address);
  if (!f) {
    static std::atomic<unsigned> _cnt{0};
    auto n = _cnt.fetch_add(1, std::memory_order_relaxed);
    if (n < 3) std::fprintf(stderr, "[sdk] unresolved indirect: 0x%08X (#%u)\n", guest_address, n + 1);
    return &__wasm_trap_handler;
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
LogCategoryId RegisterLogCategory(const char* name) {
  static std::atomic<unsigned> _c{0};
  auto n = _c.fetch_add(1, std::memory_order_relaxed);
  if (n < 3) std::fprintf(stderr, "[sdk] RegisterLogCategory: %s (#%u)\n", name, n + 1);
  return LogCategoryId(n + 1);
}
spdlog::logger* GetLoggerRaw(LogCategoryId) { return nullptr; }
std::shared_ptr<spdlog::logger> GetLogger(LogCategoryId) { return nullptr; }
std::shared_ptr<spdlog::logger> GetLogger() { return nullptr; }
}  // namespace rex

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
// MMIO handler stub — provides dummy implementations for memory-mapped I/O
MMIOHandler::MMIOHandler(uint8_t* virtual_membase, uint8_t* physical_membase, uint8_t* membase_end,
                         HostToGuestVirtual host_to_guest_virtual, const void* host_to_guest_virtual_context,
                         AccessViolationCallback access_violation_callback, void* access_violation_callback_context)
    : virtual_membase_(virtual_membase), physical_membase_(physical_membase), memory_end_(membase_end),
      host_to_guest_virtual_(host_to_guest_virtual), host_to_guest_virtual_context_(host_to_guest_virtual_context),
      access_violation_callback_(access_violation_callback), access_violation_callback_context_(access_violation_callback_context) {}

MMIOHandler* MMIOHandler::global_handler() {
  static MMIOHandler handler(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
  return &handler;
}

MMIOHandler::~MMIOHandler() = default;

bool MMIOHandler::CheckLoad(uint32_t addr, uint32_t* out) {
  static std::atomic<unsigned> _c{0};
  auto n = _c.fetch_add(1, std::memory_order_relaxed);
  if (n < 5) std::fprintf(stderr, "[sdk] MMIO CheckLoad: 0x%08X (#%u)\n", addr, n + 1);
  *out = 0;
  return true;
}

bool MMIOHandler::CheckStore(uint32_t addr, uint32_t val) {
  static std::atomic<unsigned> _c{0};
  auto n = _c.fetch_add(1, std::memory_order_relaxed);
  if (n < 5) std::fprintf(stderr, "[sdk] MMIO CheckStore: 0x%08X = 0x%08X (#%u)\n", addr, val, n + 1);
  return true;
}
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

// Forward declaration
static void preload_data_sections(uint8_t* base);

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

  // Pre-populate .rdata/.data sections with function pointers from the manifest.
  std::fprintf(stderr, "[sdk] loading data section init...\n");
  preload_data_sections(base);


  // Create a fake module handle at guest address 0x11000000.
  const uint32_t kMod = 0x11000000;
  auto* mod = reinterpret_cast<volatile uint32_t*>(base + kMod);
  mod[0] = __builtin_bswap32(0x82450000u); // vtable → entry
  mod[1] = __builtin_bswap32(0u);           // count: 0 items (skip loop)
  mod[2] = __builtin_bswap32(kMod + 16u);   // ptr to sub-struct
  // Sub-struct at kMod+16: zero-fill 60 bytes for one item
  for (int i = 4; i < 20; ++i) mod[i] = 0;

  PPCFunc* entry = disp->Get(static_cast<uint32_t>(PPCImageConfig.code_base));
  if (!entry) {
    std::fprintf(stderr, "[sdk] FATAL: entry 0x%llX not registered\n",
                 (unsigned long long)PPCImageConfig.code_base);
    return 1;
  }

  PPCContext ctx{};
  std::memset(&ctx, 0, sizeof(ctx));
  ctx.r3.u64 = kMod;
  ctx.r1.u64 = 0x40000000ull;  // 1 GB into guest space — must be inside our buffer
  ctx.r13.u64 = 0x10000000ull; // TLS base
  ctx.fpscr.InitHost();

  std::fprintf(stderr, "[sdk] calling entry 0x%llX (r1=%llX r13=%llX)\n",
               (unsigned long long)PPCImageConfig.code_base,
               (unsigned long long)ctx.r1.u64,
               (unsigned long long)ctx.r13.u64);

  // Note: Cannot set up timeout in WASM — guest may hang in tight loop
  // This is a known limitation documented in the timeout_check() function

  entry(ctx, base);
  std::fprintf(stderr, "[sdk] guest entry returned r3=0x%llX\n",
               (unsigned long long)ctx.r3.u64);

  // Probe the init chain — call more functions with the proper module handle.
  static const uint32_t init_chain[] = {
    0x82451038, 0x82451160, 0x82452620, 0x82452DC8, 0x82453280,
    0x82456000, 0x82458000, 0x8245A000, 0x8245D000, 0x82460000,
    0x82465000, 0x8246A000, 0x82470000, 0x82475000, 0x8247A000,
    0x82480000, 0x82488000, 0x82490000, 0x83060000, 0x8307A000,
    0x83100000, 0x83200000, 0x83300000, 0x83400000, 0x83500000,
    0x83600000, 0x83700000, 0x83800000,
    0, };
  for (auto* p = init_chain; *p; ++p) {
    auto* f = disp->Get(*p);
    if (!f) continue;
    PPCContext pctx{};
    std::memset(&pctx, 0, sizeof(pctx));
    pctx.r3.u64 = kMod;  // pass the fake module handle
    pctx.r1.u64 = 0x40000000ull;
    pctx.r13.u64 = 0x10000000ull;
    pctx.fpscr.InitHost();
    std::fprintf(stderr, "[sdk] calling 0x%08X...\n", *p);
    std::fflush(stderr);
    auto start = std::chrono::steady_clock::now();
    f(pctx, base);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::fprintf(stderr, "[sdk] 0x%08X → r3=0x%llX (%lld ms)\n",
                 *p, (unsigned long long)pctx.r3.u64, (long long)elapsed);
    std::fflush(stderr);
    if (elapsed > 5000) {
      std::fprintf(stderr, "[sdk] TIMEOUT — skipping rest\n");
      break;
    }
  }

  // Test: call a function known to invoke kernel stubs.
  uint32_t kernel_test = 0x83069B10;
  auto* ftest = disp->Get(kernel_test);
  if (ftest) {
    PPCContext tctx{};
    std::memset(&tctx, 0, sizeof(tctx));
    tctx.r1.u64 = 0x40000000ull;
    tctx.fpscr.InitHost();
    std::fprintf(stderr, "[sdk] kernel test: 0x%08X...\n", kernel_test);
    ftest(tctx, base);
    std::fprintf(stderr, "[sdk] kernel test returned r3=0x%llX\n",
                 (unsigned long long)tctx.r3.u64);
  }

  // Continue boot chain: call more functions that might trigger additional kernel calls
  static const uint32_t boot_chain[] = {
    0x83067690,  // KeDelayExecutionThread caller
    0x8306B6A0,  // ExCreateThread caller
    0x8306EFE0,  // Another ExCreateThread caller
    0x8306AEE8,  // RtlInitAnsiString/NtOpenFile
    0x8306AFF8,  // RtlTimeFieldsToTime
    0x83067760,  // NtOpenFile caller
    0x830691B8,  // NtOpenFile caller
    0x8306A078,  // NtOpenFile caller
    0x830ABBF8,  // XAudio function
    0x830ABD00,  // XAudio function
    0x830AC028,  // XAudio function
    0x827FE0F8,  // MmAllocatePhysicalMemoryEx caller
    0x836FB1E8,  // RtlImageXexHeaderField caller
    0x83069640,  // MmAllocatePhysicalMemoryEx caller
    // 0x83095C48,  // XMA context creation loop — SKIP to avoid infinite loop
    0x836EF540,  // TU 160 function
    // 0x836EF5A8,  // TU 160 function — SKIP (OOB crash)
    0x836EF650,  // TU 160 function
    0x8370AD68,  // TU 160 function
    0x8370B1C0,  // TU 160 function
    0x8370B458,  // TU 160 function
    0x83708828,  // TU 160 function
    0x8370A7F8,  // TU 160 function
    0x8370A9C0,  // TU 160 function
    0x8370AAA0,  // TU 160 function
    0x8370AB20,  // TU 160 function
    0x8370ABD8,  // TU 160 function
    0x83706518,  // TU 160 function
    0x83706588,  // TU 160 function
    0x837065F8,  // TU 160 function
    0x83706668,  // TU 160 function
    0x837066D8,  // TU 160 function
    0x83703768,  // TU 160 function
    0x837037E0,  // TU 160 function
    0x837037F4,  // TU 160 function
    0x837037F8,  // TU 160 function
    // 0x83703870,  // TU 160 function — SKIP (stack overflow)
    0x836FF6A0,  // TU 160 function
    0x836FF710,  // TU 160 function
    0x836FF9C0,  // TU 160 function
    0x836FFA20,  // TU 160 function
    0x836FFA90,  // TU 160 function
    0x836FC8E0,  // TU 160 function
    0x836FC8F8,  // TU 160 function
    0x836FC910,  // TU 160 function
    0x836FC928,  // TU 160 function
    0x836FCA50,  // TU 160 function
    0x836FB2E8,  // TU 160 function
    0x836FB370,  // TU 160 function
    0x836FB4B8,  // TU 160 function
    0x836FB5D0,  // TU 160 function
    0x836FB6A0,  // TU 160 function
    0x836FA100,  // TU 160 function
    0x836FA178,  // TU 160 function
    0x836FA188,  // TU 160 function
    0x836FA200,  // TU 160 function
    0x836FA210,  // TU 160 function
    0x836FA278,  // TU 160 function
    0, };
  for (auto* p = boot_chain; *p; ++p) {
    auto* f = disp->Get(*p);
    if (!f) continue;
    PPCContext pctx{};
    std::memset(&pctx, 0, sizeof(pctx));
    pctx.r3.u64 = kMod;
    pctx.r1.u64 = 0x40000000ull;
    pctx.r13.u64 = 0x10000000ull;
    pctx.fpscr.InitHost();
    std::fprintf(stderr, "[sdk] calling 0x%08X...\n", *p);
    auto start = std::chrono::steady_clock::now();
    f(pctx, base);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::fprintf(stderr, "[sdk] 0x%08X → r3=0x%llX (%lld ms)\n",
                 *p, (unsigned long long)pctx.r3.u64, (long long)elapsed);
    if (elapsed > 5000) {
      std::fprintf(stderr, "[sdk] TIMEOUT — skipping rest\n");
      break;
    }
  }

  return 0;
}
// Auto-generated: 354 data section initializers from nhllegacy_functions.toml
static void preload_data_sections(uint8_t* base) {
  *((volatile uint32_t*)(base + 0x83B4B15C)) = __builtin_bswap32(2185793856u);  // -> 0x82489140
  *((volatile uint32_t*)(base + 0x83B4B1F8)) = __builtin_bswap32(2185806224u);  // -> 0x8248C190
  *((volatile uint32_t*)(base + 0x83B4B1FC)) = __builtin_bswap32(2185806280u);  // -> 0x8248C1C8
  *((volatile uint32_t*)(base + 0x83B4B200)) = __builtin_bswap32(2185806280u);  // -> 0x8248C1C8
  *((volatile uint32_t*)(base + 0x823302B8)) = __builtin_bswap32(2185862880u);  // -> 0x82499EE0
  *((volatile uint32_t*)(base + 0x82332E1C)) = __builtin_bswap32(2186025624u);  // -> 0x824C1A98
  *((volatile uint32_t*)(base + 0x82332E88)) = __builtin_bswap32(2186027832u);  // -> 0x824C2338
  *((volatile uint32_t*)(base + 0x82332E84)) = __builtin_bswap32(2186027912u);  // -> 0x824C2388
  *((volatile uint32_t*)(base + 0x82332E78)) = __builtin_bswap32(2186028224u);  // -> 0x824C24C0
  *((volatile uint32_t*)(base + 0x82332E74)) = __builtin_bswap32(2186028304u);  // -> 0x824C2510
  *((volatile uint32_t*)(base + 0x82332E70)) = __builtin_bswap32(2186028384u);  // -> 0x824C2560
  *((volatile uint32_t*)(base + 0x82349EC4)) = __builtin_bswap32(2186077216u);  // -> 0x824CE420
  *((volatile uint32_t*)(base + 0x823346B0)) = __builtin_bswap32(2186097616u);  // -> 0x824D33D0
  *((volatile uint32_t*)(base + 0x823348A8)) = __builtin_bswap32(2186206344u);  // -> 0x824EDC88
  *((volatile uint32_t*)(base + 0x823349B8)) = __builtin_bswap32(2186206392u);  // -> 0x824EDCB8
  *((volatile uint32_t*)(base + 0x82334A38)) = __builtin_bswap32(2186206432u);  // -> 0x824EDCE0
  *((volatile uint32_t*)(base + 0x8233498C)) = __builtin_bswap32(2186206456u);  // -> 0x824EDCF8
  *((volatile uint32_t*)(base + 0x82349C58)) = __builtin_bswap32(2186670432u);  // -> 0x8255F160
  *((volatile uint32_t*)(base + 0x82349ED8)) = __builtin_bswap32(2186683064u);  // -> 0x825622B8
  *((volatile uint32_t*)(base + 0x8234987C)) = __builtin_bswap32(2186683424u);  // -> 0x82562420
  *((volatile uint32_t*)(base + 0x82349DB8)) = __builtin_bswap32(2186683424u);  // -> 0x82562420
  *((volatile uint32_t*)(base + 0x823497A4)) = __builtin_bswap32(2186683576u);  // -> 0x825624B8
  *((volatile uint32_t*)(base + 0x8234B534)) = __builtin_bswap32(2186712440u);  // -> 0x82569578
  *((volatile uint32_t*)(base + 0x8234D238)) = __builtin_bswap32(2186988176u);  // -> 0x825ACA90
  *((volatile uint32_t*)(base + 0x8234E038)) = __builtin_bswap32(2187136520u);  // -> 0x825D0E08
  *((volatile uint32_t*)(base + 0x8234E128)) = __builtin_bswap32(2187136520u);  // -> 0x825D0E08
  *((volatile uint32_t*)(base + 0x82391600)) = __builtin_bswap32(2187731568u);  // -> 0x82662270
  *((volatile uint32_t*)(base + 0x82393000)) = __builtin_bswap32(2187808448u);  // -> 0x82674EC0
  *((volatile uint32_t*)(base + 0x82392FF8)) = __builtin_bswap32(2187809648u);  // -> 0x82675370
  *((volatile uint32_t*)(base + 0x82392FF4)) = __builtin_bswap32(2187809672u);  // -> 0x82675388
  *((volatile uint32_t*)(base + 0x82393008)) = __builtin_bswap32(2187809672u);  // -> 0x82675388
  *((volatile uint32_t*)(base + 0x83B3BA90)) = __builtin_bswap32(2188093864u);  // -> 0x826BA9A8
  *((volatile uint32_t*)(base + 0x83B3BB54)) = __builtin_bswap32(2188093864u);  // -> 0x826BA9A8
  *((volatile uint32_t*)(base + 0x8239AE94)) = __builtin_bswap32(2188355424u);  // -> 0x826FA760
  *((volatile uint32_t*)(base + 0x8239AE98)) = __builtin_bswap32(2188355448u);  // -> 0x826FA778
  *((volatile uint32_t*)(base + 0x83B3AA20)) = __builtin_bswap32(2188784136u);  // -> 0x82763208
  *((volatile uint32_t*)(base + 0x823A5940)) = __builtin_bswap32(2189054384u);  // -> 0x827A51B0
  *((volatile uint32_t*)(base + 0x823A9A34)) = __builtin_bswap32(2189501936u);  // -> 0x828125F0
  *((volatile uint32_t*)(base + 0x822F64FC)) = __builtin_bswap32(2189800680u);  // -> 0x8285B4E8
  *((volatile uint32_t*)(base + 0x823B01C8)) = __builtin_bswap32(2190064992u);  // -> 0x8289BD60
  *((volatile uint32_t*)(base + 0x823B0210)) = __builtin_bswap32(2190071092u);  // -> 0x8289D534
  *((volatile uint32_t*)(base + 0x823B0220)) = __builtin_bswap32(2190071468u);  // -> 0x8289D6AC
  *((volatile uint32_t*)(base + 0x823B0260)) = __builtin_bswap32(2190074348u);  // -> 0x8289E1EC
  *((volatile uint32_t*)(base + 0x820144D0)) = __builtin_bswap32(2190101568u);  // -> 0x828A4C40
  *((volatile uint32_t*)(base + 0x820144D4)) = __builtin_bswap32(2190101592u);  // -> 0x828A4C58
  *((volatile uint32_t*)(base + 0x820144D8)) = __builtin_bswap32(2190101616u);  // -> 0x828A4C70
  *((volatile uint32_t*)(base + 0x8201CB68)) = __builtin_bswap32(2190219176u);  // -> 0x828C17A8
  *((volatile uint32_t*)(base + 0x82022740)) = __builtin_bswap32(2190595768u);  // -> 0x8291D6B8
  *((volatile uint32_t*)(base + 0x82037A5C)) = __builtin_bswap32(2191032680u);  // -> 0x82988168
  *((volatile uint32_t*)(base + 0x82046E20)) = __builtin_bswap32(2191278832u);  // -> 0x829C42F0
  *((volatile uint32_t*)(base + 0x82046E2C)) = __builtin_bswap32(2191278840u);  // -> 0x829C42F8
  *((volatile uint32_t*)(base + 0x82046EA4)) = __builtin_bswap32(2191281912u);  // -> 0x829C4EF8
  *((volatile uint32_t*)(base + 0x82046EA8)) = __builtin_bswap32(2191281920u);  // -> 0x829C4F00
  *((volatile uint32_t*)(base + 0x82046EAC)) = __builtin_bswap32(2191281928u);  // -> 0x829C4F08
  *((volatile uint32_t*)(base + 0x82046EB0)) = __builtin_bswap32(2191281936u);  // -> 0x829C4F10
  *((volatile uint32_t*)(base + 0x82046EB4)) = __builtin_bswap32(2191281944u);  // -> 0x829C4F18
  *((volatile uint32_t*)(base + 0x82046EB8)) = __builtin_bswap32(2191281952u);  // -> 0x829C4F20
  *((volatile uint32_t*)(base + 0x82046EBC)) = __builtin_bswap32(2191281960u);  // -> 0x829C4F28
  *((volatile uint32_t*)(base + 0x82046EC0)) = __builtin_bswap32(2191281968u);  // -> 0x829C4F30
  *((volatile uint32_t*)(base + 0x82046EC8)) = __builtin_bswap32(2191281984u);  // -> 0x829C4F40
  *((volatile uint32_t*)(base + 0x82046ECC)) = __builtin_bswap32(2191281992u);  // -> 0x829C4F48
  *((volatile uint32_t*)(base + 0x82046ED8)) = __builtin_bswap32(2191282016u);  // -> 0x829C4F60
  *((volatile uint32_t*)(base + 0x82046E80)) = __builtin_bswap32(2191282024u);  // -> 0x829C4F68
  *((volatile uint32_t*)(base + 0x82046F14)) = __builtin_bswap32(2191282328u);  // -> 0x829C5098
  *((volatile uint32_t*)(base + 0x82046F18)) = __builtin_bswap32(2191282344u);  // -> 0x829C50A8
  *((volatile uint32_t*)(base + 0x82047B5C)) = __builtin_bswap32(2191315128u);  // -> 0x829CD0B8
  *((volatile uint32_t*)(base + 0x82047B78)) = __builtin_bswap32(2191315272u);  // -> 0x829CD148
  *((volatile uint32_t*)(base + 0x82047B84)) = __builtin_bswap32(2191315336u);  // -> 0x829CD188
  *((volatile uint32_t*)(base + 0x82047B8C)) = __builtin_bswap32(2191315592u);  // -> 0x829CD288
  *((volatile uint32_t*)(base + 0x82047B94)) = __builtin_bswap32(2191315600u);  // -> 0x829CD290
  *((volatile uint32_t*)(base + 0x8204A784)) = __builtin_bswap32(2191416680u);  // -> 0x829E5D68
  *((volatile uint32_t*)(base + 0x8204A80C)) = __builtin_bswap32(2191416696u);  // -> 0x829E5D78
  *((volatile uint32_t*)(base + 0x8204A840)) = __builtin_bswap32(2191416920u);  // -> 0x829E5E58
  *((volatile uint32_t*)(base + 0x8204A84C)) = __builtin_bswap32(2191416944u);  // -> 0x829E5E70
  *((volatile uint32_t*)(base + 0x8204A8A8)) = __builtin_bswap32(2191416960u);  // -> 0x829E5E80
  *((volatile uint32_t*)(base + 0x8205A3EC)) = __builtin_bswap32(2191657752u);  // -> 0x82A20B18
  *((volatile uint32_t*)(base + 0x8208B3BC)) = __builtin_bswap32(2191657752u);  // -> 0x82A20B18
  *((volatile uint32_t*)(base + 0x820EBAF4)) = __builtin_bswap32(2191657752u);  // -> 0x82A20B18
  *((volatile uint32_t*)(base + 0x8205A3FC)) = __builtin_bswap32(2191657768u);  // -> 0x82A20B28
  *((volatile uint32_t*)(base + 0x820807B4)) = __builtin_bswap32(2191657768u);  // -> 0x82A20B28
  *((volatile uint32_t*)(base + 0x8208B3CC)) = __builtin_bswap32(2191657768u);  // -> 0x82A20B28
  *((volatile uint32_t*)(base + 0x820667F4)) = __builtin_bswap32(2192035000u);  // -> 0x82A7CCB8
  *((volatile uint32_t*)(base + 0x8208BEF0)) = __builtin_bswap32(2193321024u);  // -> 0x82BB6C40
  *((volatile uint32_t*)(base + 0x82091598)) = __builtin_bswap32(2193386216u);  // -> 0x82BC6AE8
  *((volatile uint32_t*)(base + 0x8209159C)) = __builtin_bswap32(2193386240u);  // -> 0x82BC6B00
  *((volatile uint32_t*)(base + 0x820915A4)) = __builtin_bswap32(2193386264u);  // -> 0x82BC6B18
  *((volatile uint32_t*)(base + 0x820915A0)) = __builtin_bswap32(2193386288u);  // -> 0x82BC6B30
  *((volatile uint32_t*)(base + 0x820915AC)) = __builtin_bswap32(2193386328u);  // -> 0x82BC6B58
  *((volatile uint32_t*)(base + 0x820915B4)) = __builtin_bswap32(2193386352u);  // -> 0x82BC6B70
  *((volatile uint32_t*)(base + 0x820915B8)) = __builtin_bswap32(2193386376u);  // -> 0x82BC6B88
  *((volatile uint32_t*)(base + 0x820915BC)) = __builtin_bswap32(2193386400u);  // -> 0x82BC6BA0
  *((volatile uint32_t*)(base + 0x820CDAB0)) = __builtin_bswap32(2194012768u);  // -> 0x82C5FA60
  *((volatile uint32_t*)(base + 0x8209D830)) = __builtin_bswap32(2194020072u);  // -> 0x82C616E8
  *((volatile uint32_t*)(base + 0x820A9910)) = __builtin_bswap32(2194446840u);  // -> 0x82CC99F8
  *((volatile uint32_t*)(base + 0x820AAA08)) = __builtin_bswap32(2194582432u);  // -> 0x82CEABA0
  *((volatile uint32_t*)(base + 0x820ABA90)) = __builtin_bswap32(2194630024u);  // -> 0x82CF6588
  *((volatile uint32_t*)(base + 0x820B3890)) = __builtin_bswap32(2194949936u);  // -> 0x82D44730
  *((volatile uint32_t*)(base + 0x820B4FAC)) = __builtin_bswap32(2194994968u);  // -> 0x82D4F718
  *((volatile uint32_t*)(base + 0x820CB520)) = __builtin_bswap32(2195641120u);  // -> 0x82DED320
  *((volatile uint32_t*)(base + 0x820CDAFC)) = __builtin_bswap32(2195649240u);  // -> 0x82DEF2D8
  *((volatile uint32_t*)(base + 0x820CADCC)) = __builtin_bswap32(2195657976u);  // -> 0x82DF14F8
  *((volatile uint32_t*)(base + 0x820CDF2C)) = __builtin_bswap32(2195690288u);  // -> 0x82DF9330
  *((volatile uint32_t*)(base + 0x820CE31C)) = __builtin_bswap32(2195690288u);  // -> 0x82DF9330
  *((volatile uint32_t*)(base + 0x820CE4A4)) = __builtin_bswap32(2195693472u);  // -> 0x82DF9FA0
  *((volatile uint32_t*)(base + 0x820CE51C)) = __builtin_bswap32(2195697328u);  // -> 0x82DFAEB0
  *((volatile uint32_t*)(base + 0x820E6808)) = __builtin_bswap32(2196106920u);  // -> 0x82E5EEA8
  *((volatile uint32_t*)(base + 0x820E680C)) = __builtin_bswap32(2196106920u);  // -> 0x82E5EEA8
  *((volatile uint32_t*)(base + 0x820E7550)) = __builtin_bswap32(2196106920u);  // -> 0x82E5EEA8
  *((volatile uint32_t*)(base + 0x820E869C)) = __builtin_bswap32(2196114304u);  // -> 0x82E60B80
  *((volatile uint32_t*)(base + 0x820E86B4)) = __builtin_bswap32(2196114336u);  // -> 0x82E60BA0
  *((volatile uint32_t*)(base + 0x820E86BC)) = __builtin_bswap32(2196114368u);  // -> 0x82E60BC0
  *((volatile uint32_t*)(base + 0x820E86DC)) = __builtin_bswap32(2196114400u);  // -> 0x82E60BE0
  *((volatile uint32_t*)(base + 0x820E6EE8)) = __builtin_bswap32(2196114944u);  // -> 0x82E60E00
  *((volatile uint32_t*)(base + 0x820E6EFC)) = __builtin_bswap32(2196115104u);  // -> 0x82E60EA0
  *((volatile uint32_t*)(base + 0x820E5E7C)) = __builtin_bswap32(2196118976u);  // -> 0x82E61DC0
  *((volatile uint32_t*)(base + 0x820E7450)) = __builtin_bswap32(2196138896u);  // -> 0x82E66B90
  *((volatile uint32_t*)(base + 0x820F1308)) = __builtin_bswap32(2196138896u);  // -> 0x82E66B90
  *((volatile uint32_t*)(base + 0x820E7780)) = __builtin_bswap32(2196146672u);  // -> 0x82E689F0
  *((volatile uint32_t*)(base + 0x820E5114)) = __builtin_bswap32(2196155008u);  // -> 0x82E6AA80
  *((volatile uint32_t*)(base + 0x820E6994)) = __builtin_bswap32(2196155008u);  // -> 0x82E6AA80
  *((volatile uint32_t*)(base + 0x820E50F0)) = __builtin_bswap32(2196155160u);  // -> 0x82E6AB18
  *((volatile uint32_t*)(base + 0x820E6970)) = __builtin_bswap32(2196155160u);  // -> 0x82E6AB18
  *((volatile uint32_t*)(base + 0x820E6E1C)) = __builtin_bswap32(2196183448u);  // -> 0x82E71998
  *((volatile uint32_t*)(base + 0x820E8AE0)) = __builtin_bswap32(2196203688u);  // -> 0x82E768A8
  *((volatile uint32_t*)(base + 0x820E8AD8)) = __builtin_bswap32(2196203768u);  // -> 0x82E768F8
  *((volatile uint32_t*)(base + 0x820E8AC8)) = __builtin_bswap32(2196203832u);  // -> 0x82E76938
  *((volatile uint32_t*)(base + 0x820E8AF0)) = __builtin_bswap32(2196203856u);  // -> 0x82E76950
  *((volatile uint32_t*)(base + 0x820E8AF4)) = __builtin_bswap32(2196203880u);  // -> 0x82E76968
  *((volatile uint32_t*)(base + 0x820E8AF8)) = __builtin_bswap32(2196203928u);  // -> 0x82E76998
  *((volatile uint32_t*)(base + 0x820E8B14)) = __builtin_bswap32(2196204048u);  // -> 0x82E76A10
  *((volatile uint32_t*)(base + 0x820E8B1C)) = __builtin_bswap32(2196204072u);  // -> 0x82E76A28
  *((volatile uint32_t*)(base + 0x820E8B48)) = __builtin_bswap32(2196204120u);  // -> 0x82E76A58
  *((volatile uint32_t*)(base + 0x820E8B4C)) = __builtin_bswap32(2196204144u);  // -> 0x82E76A70
  *((volatile uint32_t*)(base + 0x820E5104)) = __builtin_bswap32(2196243560u);  // -> 0x82E80468
  *((volatile uint32_t*)(base + 0x820E6984)) = __builtin_bswap32(2196243560u);  // -> 0x82E80468
  *((volatile uint32_t*)(base + 0x820E69A0)) = __builtin_bswap32(2196246680u);  // -> 0x82E81098
  *((volatile uint32_t*)(base + 0x820E699C)) = __builtin_bswap32(2196246688u);  // -> 0x82E810A0
  *((volatile uint32_t*)(base + 0x820E6998)) = __builtin_bswap32(2196246696u);  // -> 0x82E810A8
  *((volatile uint32_t*)(base + 0x820E6C4C)) = __builtin_bswap32(2196264160u);  // -> 0x82E854E0
  *((volatile uint32_t*)(base + 0x820E6C74)) = __builtin_bswap32(2196264192u);  // -> 0x82E85500
  *((volatile uint32_t*)(base + 0x820E6E78)) = __builtin_bswap32(2196275032u);  // -> 0x82E87F58
  *((volatile uint32_t*)(base + 0x820E6E50)) = __builtin_bswap32(2196275056u);  // -> 0x82E87F70
  *((volatile uint32_t*)(base + 0x820E6F98)) = __builtin_bswap32(2196275056u);  // -> 0x82E87F70
  *((volatile uint32_t*)(base + 0x820E7470)) = __builtin_bswap32(2196275056u);  // -> 0x82E87F70
  *((volatile uint32_t*)(base + 0x820E6E3C)) = __builtin_bswap32(2196275064u);  // -> 0x82E87F78
  *((volatile uint32_t*)(base + 0x820E6F84)) = __builtin_bswap32(2196275064u);  // -> 0x82E87F78
  *((volatile uint32_t*)(base + 0x820E745C)) = __builtin_bswap32(2196275064u);  // -> 0x82E87F78
  *((volatile uint32_t*)(base + 0x820E6E44)) = __builtin_bswap32(2196275080u);  // -> 0x82E87F88
  *((volatile uint32_t*)(base + 0x820E6F8C)) = __builtin_bswap32(2196275080u);  // -> 0x82E87F88
  *((volatile uint32_t*)(base + 0x820E7464)) = __builtin_bswap32(2196275080u);  // -> 0x82E87F88
  *((volatile uint32_t*)(base + 0x820E6E70)) = __builtin_bswap32(2196275104u);  // -> 0x82E87FA0
  *((volatile uint32_t*)(base + 0x820E6FB8)) = __builtin_bswap32(2196275104u);  // -> 0x82E87FA0
  *((volatile uint32_t*)(base + 0x820E6E54)) = __builtin_bswap32(2196308008u);  // -> 0x82E90028
  *((volatile uint32_t*)(base + 0x820E6F9C)) = __builtin_bswap32(2196308008u);  // -> 0x82E90028
  *((volatile uint32_t*)(base + 0x820E7474)) = __builtin_bswap32(2196308008u);  // -> 0x82E90028
  *((volatile uint32_t*)(base + 0x820E6EB0)) = __builtin_bswap32(2196308016u);  // -> 0x82E90030
  *((volatile uint32_t*)(base + 0x820E6FF8)) = __builtin_bswap32(2196308016u);  // -> 0x82E90030
  *((volatile uint32_t*)(base + 0x820E74D0)) = __builtin_bswap32(2196308016u);  // -> 0x82E90030
  *((volatile uint32_t*)(base + 0x820E6EB8)) = __builtin_bswap32(2196308056u);  // -> 0x82E90058
  *((volatile uint32_t*)(base + 0x820E7000)) = __builtin_bswap32(2196308056u);  // -> 0x82E90058
  *((volatile uint32_t*)(base + 0x820E74D8)) = __builtin_bswap32(2196308056u);  // -> 0x82E90058
  *((volatile uint32_t*)(base + 0x820E74E4)) = __builtin_bswap32(2196308096u);  // -> 0x82E90080
  *((volatile uint32_t*)(base + 0x820E6E64)) = __builtin_bswap32(2196308616u);  // -> 0x82E90288
  *((volatile uint32_t*)(base + 0x820E6FAC)) = __builtin_bswap32(2196308616u);  // -> 0x82E90288
  *((volatile uint32_t*)(base + 0x820E7484)) = __builtin_bswap32(2196308616u);  // -> 0x82E90288
  *((volatile uint32_t*)(base + 0x820E6E5C)) = __builtin_bswap32(2196308624u);  // -> 0x82E90290
  *((volatile uint32_t*)(base + 0x820E6FA4)) = __builtin_bswap32(2196308624u);  // -> 0x82E90290
  *((volatile uint32_t*)(base + 0x820E747C)) = __builtin_bswap32(2196308624u);  // -> 0x82E90290
  *((volatile uint32_t*)(base + 0x820E7490)) = __builtin_bswap32(2196308632u);  // -> 0x82E90298
  *((volatile uint32_t*)(base + 0x820E8020)) = __builtin_bswap32(2196345224u);  // -> 0x82E99188
  *((volatile uint32_t*)(base + 0x820E86C4)) = __builtin_bswap32(2196368904u);  // -> 0x82E9EE08
  *((volatile uint32_t*)(base + 0x820E86C8)) = __builtin_bswap32(2196368936u);  // -> 0x82E9EE28
  *((volatile uint32_t*)(base + 0x820E86D4)) = __builtin_bswap32(2196368968u);  // -> 0x82E9EE48
  *((volatile uint32_t*)(base + 0x820E86D8)) = __builtin_bswap32(2196369000u);  // -> 0x82E9EE68
  *((volatile uint32_t*)(base + 0x820E8704)) = __builtin_bswap32(2196369032u);  // -> 0x82E9EE88
  *((volatile uint32_t*)(base + 0x820E8708)) = __builtin_bswap32(2196369064u);  // -> 0x82E9EEA8
  *((volatile uint32_t*)(base + 0x820E870C)) = __builtin_bswap32(2196369096u);  // -> 0x82E9EEC8
  *((volatile uint32_t*)(base + 0x820E86E0)) = __builtin_bswap32(2196369128u);  // -> 0x82E9EEE8
  *((volatile uint32_t*)(base + 0x820E8AA4)) = __builtin_bswap32(2196380816u);  // -> 0x82EA1C90
  *((volatile uint32_t*)(base + 0x820E8AA8)) = __builtin_bswap32(2196380840u);  // -> 0x82EA1CA8
  *((volatile uint32_t*)(base + 0x820E8A9C)) = __builtin_bswap32(2196380864u);  // -> 0x82EA1CC0
  *((volatile uint32_t*)(base + 0x820E8A94)) = __builtin_bswap32(2196380888u);  // -> 0x82EA1CD8
  *((volatile uint32_t*)(base + 0x820E8AA0)) = __builtin_bswap32(2196380912u);  // -> 0x82EA1CF0
  *((volatile uint32_t*)(base + 0x820E8AAC)) = __builtin_bswap32(2196380936u);  // -> 0x82EA1D08
  *((volatile uint32_t*)(base + 0x820E8A98)) = __builtin_bswap32(2196380960u);  // -> 0x82EA1D20
  *((volatile uint32_t*)(base + 0x820E8AB0)) = __builtin_bswap32(2196380984u);  // -> 0x82EA1D38
  *((volatile uint32_t*)(base + 0x820E8AB4)) = __builtin_bswap32(2196381008u);  // -> 0x82EA1D50
  *((volatile uint32_t*)(base + 0x820E8B68)) = __builtin_bswap32(2196381032u);  // -> 0x82EA1D68
  *((volatile uint32_t*)(base + 0x820E8B6C)) = __builtin_bswap32(2196381056u);  // -> 0x82EA1D80
  *((volatile uint32_t*)(base + 0x820E8B70)) = __builtin_bswap32(2196381080u);  // -> 0x82EA1D98
  *((volatile uint32_t*)(base + 0x820E8B74)) = __builtin_bswap32(2196381104u);  // -> 0x82EA1DB0
  *((volatile uint32_t*)(base + 0x820E8B38)) = __builtin_bswap32(2196381128u);  // -> 0x82EA1DC8
  *((volatile uint32_t*)(base + 0x820E6B10)) = __builtin_bswap32(2196381960u);  // -> 0x82EA2108
  *((volatile uint32_t*)(base + 0x820E8C88)) = __builtin_bswap32(2196381960u);  // -> 0x82EA2108
  *((volatile uint32_t*)(base + 0x820F9AE4)) = __builtin_bswap32(2196381960u);  // -> 0x82EA2108
  *((volatile uint32_t*)(base + 0x820E8DD8)) = __builtin_bswap32(2196384464u);  // -> 0x82EA2AD0
  *((volatile uint32_t*)(base + 0x820E8E50)) = __builtin_bswap32(2196384592u);  // -> 0x82EA2B50
  *((volatile uint32_t*)(base + 0x820E8E70)) = __builtin_bswap32(2196384768u);  // -> 0x82EA2C00
  *((volatile uint32_t*)(base + 0x820ED6C0)) = __builtin_bswap32(2196501784u);  // -> 0x82EBF518
  *((volatile uint32_t*)(base + 0x820F20E0)) = __builtin_bswap32(2196523824u);  // -> 0x82EC4B30
  *((volatile uint32_t*)(base + 0x820F20E4)) = __builtin_bswap32(2196523824u);  // -> 0x82EC4B30
  *((volatile uint32_t*)(base + 0x820EDD70)) = __builtin_bswap32(2196528472u);  // -> 0x82EC5D58
  *((volatile uint32_t*)(base + 0x820ECC74)) = __builtin_bswap32(2196552832u);  // -> 0x82ECBC80
  *((volatile uint32_t*)(base + 0x820ECC78)) = __builtin_bswap32(2196552904u);  // -> 0x82ECBCC8
  *((volatile uint32_t*)(base + 0x820F13C8)) = __builtin_bswap32(2196552904u);  // -> 0x82ECBCC8
  *((volatile uint32_t*)(base + 0x820F0024)) = __builtin_bswap32(2196564760u);  // -> 0x82ECEB18
  *((volatile uint32_t*)(base + 0x820EFABC)) = __builtin_bswap32(2196592480u);  // -> 0x82ED5760
  *((volatile uint32_t*)(base + 0x820EFAC8)) = __builtin_bswap32(2196592776u);  // -> 0x82ED5888
  *((volatile uint32_t*)(base + 0x820EFB34)) = __builtin_bswap32(2196593208u);  // -> 0x82ED5A38
  *((volatile uint32_t*)(base + 0x820EFB4C)) = __builtin_bswap32(2196593480u);  // -> 0x82ED5B48
  *((volatile uint32_t*)(base + 0x820F00D8)) = __builtin_bswap32(2196652096u);  // -> 0x82EE4040
  *((volatile uint32_t*)(base + 0x820EFA98)) = __builtin_bswap32(2196699568u);  // -> 0x82EEF9B0
  *((volatile uint32_t*)(base + 0x820F031C)) = __builtin_bswap32(2196768544u);  // -> 0x82F00720
  *((volatile uint32_t*)(base + 0x820F5EDC)) = __builtin_bswap32(2196904568u);  // -> 0x82F21A78
  *((volatile uint32_t*)(base + 0x820F5F7C)) = __builtin_bswap32(2196905632u);  // -> 0x82F21EA0
  *((volatile uint32_t*)(base + 0x820FBA08)) = __builtin_bswap32(2196906136u);  // -> 0x82F22098
  *((volatile uint32_t*)(base + 0x820F3D30)) = __builtin_bswap32(2196908048u);  // -> 0x82F22810
  *((volatile uint32_t*)(base + 0x820F3D7C)) = __builtin_bswap32(2196908048u);  // -> 0x82F22810
  *((volatile uint32_t*)(base + 0x820F5150)) = __builtin_bswap32(2196955320u);  // -> 0x82F2E0B8
  *((volatile uint32_t*)(base + 0x820F54F4)) = __builtin_bswap32(2196959760u);  // -> 0x82F2F210
  *((volatile uint32_t*)(base + 0x820F5508)) = __builtin_bswap32(2196959760u);  // -> 0x82F2F210
  *((volatile uint32_t*)(base + 0x820F553C)) = __builtin_bswap32(2196959760u);  // -> 0x82F2F210
  *((volatile uint32_t*)(base + 0x820F54F8)) = __builtin_bswap32(2196959792u);  // -> 0x82F2F230
  *((volatile uint32_t*)(base + 0x820F550C)) = __builtin_bswap32(2196959792u);  // -> 0x82F2F230
  *((volatile uint32_t*)(base + 0x820F5540)) = __builtin_bswap32(2196959792u);  // -> 0x82F2F230
  *((volatile uint32_t*)(base + 0x820F5EC0)) = __builtin_bswap32(2196982192u);  // -> 0x82F349B0
  *((volatile uint32_t*)(base + 0x820F5E98)) = __builtin_bswap32(2196982224u);  // -> 0x82F349D0
  *((volatile uint32_t*)(base + 0x820F5FAC)) = __builtin_bswap32(2196982992u);  // -> 0x82F34CD0
  *((volatile uint32_t*)(base + 0x820F5F80)) = __builtin_bswap32(2196983024u);  // -> 0x82F34CF0
  *((volatile uint32_t*)(base + 0x820F8B58)) = __builtin_bswap32(2196984032u);  // -> 0x82F350E0
  *((volatile uint32_t*)(base + 0x820F8C28)) = __builtin_bswap32(2196984816u);  // -> 0x82F353F0
  *((volatile uint32_t*)(base + 0x820F8C38)) = __builtin_bswap32(2196984968u);  // -> 0x82F35488
  *((volatile uint32_t*)(base + 0x8213D110)) = __builtin_bswap32(2196984968u);  // -> 0x82F35488
  *((volatile uint32_t*)(base + 0x8213D238)) = __builtin_bswap32(2196984968u);  // -> 0x82F35488
  *((volatile uint32_t*)(base + 0x820F8C4C)) = __builtin_bswap32(2196985120u);  // -> 0x82F35520
  *((volatile uint32_t*)(base + 0x820FBA58)) = __builtin_bswap32(2197220104u);  // -> 0x82F6EB08
  *((volatile uint32_t*)(base + 0x820FBA30)) = __builtin_bswap32(2197220136u);  // -> 0x82F6EB28
  *((volatile uint32_t*)(base + 0x82101A60)) = __builtin_bswap32(2197356144u);  // -> 0x82F8FE70
  *((volatile uint32_t*)(base + 0x82102BA4)) = __builtin_bswap32(2197496752u);  // -> 0x82FB23B0
  *((volatile uint32_t*)(base + 0x82116D90)) = __builtin_bswap32(2197733376u);  // -> 0x82FEC000
  *((volatile uint32_t*)(base + 0x83A639DC)) = __builtin_bswap32(2197888600u);  // -> 0x83011E58
  *((volatile uint32_t*)(base + 0x823B0460)) = __builtin_bswap32(2198329908u);  // -> 0x8307DA34
  *((volatile uint32_t*)(base + 0x823B0490)) = __builtin_bswap32(2198331024u);  // -> 0x8307DE90
  *((volatile uint32_t*)(base + 0x823B05C8)) = __builtin_bswap32(2198368312u);  // -> 0x83087038
  *((volatile uint32_t*)(base + 0x823B0690)) = __builtin_bswap32(2198376532u);  // -> 0x83089054
  *((volatile uint32_t*)(base + 0x823B06B0)) = __builtin_bswap32(2198388176u);  // -> 0x8308BDD0
  *((volatile uint32_t*)(base + 0x8213CFF0)) = __builtin_bswap32(2198397864u);  // -> 0x8308E3A8
  *((volatile uint32_t*)(base + 0x8213D0A4)) = __builtin_bswap32(2198407136u);  // -> 0x830907E0
  *((volatile uint32_t*)(base + 0x8213D1CC)) = __builtin_bswap32(2198407136u);  // -> 0x830907E0
  *((volatile uint32_t*)(base + 0x8213D374)) = __builtin_bswap32(2198407136u);  // -> 0x830907E0
  *((volatile uint32_t*)(base + 0x8213D0C4)) = __builtin_bswap32(2198407144u);  // -> 0x830907E8
  *((volatile uint32_t*)(base + 0x8213D1EC)) = __builtin_bswap32(2198407144u);  // -> 0x830907E8
  *((volatile uint32_t*)(base + 0x8213D394)) = __builtin_bswap32(2198407144u);  // -> 0x830907E8
  *((volatile uint32_t*)(base + 0x8213D0B0)) = __builtin_bswap32(2198407152u);  // -> 0x830907F0
  *((volatile uint32_t*)(base + 0x8213D1D8)) = __builtin_bswap32(2198407152u);  // -> 0x830907F0
  *((volatile uint32_t*)(base + 0x8213D380)) = __builtin_bswap32(2198407152u);  // -> 0x830907F0
  *((volatile uint32_t*)(base + 0x8213D0EC)) = __builtin_bswap32(2198407160u);  // -> 0x830907F8
  *((volatile uint32_t*)(base + 0x8213D0CC)) = __builtin_bswap32(2198407176u);  // -> 0x83090808
  *((volatile uint32_t*)(base + 0x8213D1F4)) = __builtin_bswap32(2198407176u);  // -> 0x83090808
  *((volatile uint32_t*)(base + 0x8213D39C)) = __builtin_bswap32(2198407176u);  // -> 0x83090808
  *((volatile uint32_t*)(base + 0x8213D0E8)) = __builtin_bswap32(2198407184u);  // -> 0x83090810
  *((volatile uint32_t*)(base + 0x8213D210)) = __builtin_bswap32(2198407184u);  // -> 0x83090810
  *((volatile uint32_t*)(base + 0x8213D3B8)) = __builtin_bswap32(2198407184u);  // -> 0x83090810
  *((volatile uint32_t*)(base + 0x8213D0BC)) = __builtin_bswap32(2198407192u);  // -> 0x83090818
  *((volatile uint32_t*)(base + 0x8213D1E4)) = __builtin_bswap32(2198407192u);  // -> 0x83090818
  *((volatile uint32_t*)(base + 0x8213D38C)) = __builtin_bswap32(2198407192u);  // -> 0x83090818
  *((volatile uint32_t*)(base + 0x8213D10C)) = __builtin_bswap32(2198407200u);  // -> 0x83090820
  *((volatile uint32_t*)(base + 0x8213D234)) = __builtin_bswap32(2198407200u);  // -> 0x83090820
  *((volatile uint32_t*)(base + 0x8213D3DC)) = __builtin_bswap32(2198407200u);  // -> 0x83090820
  *((volatile uint32_t*)(base + 0x8213D0A0)) = __builtin_bswap32(2198407288u);  // -> 0x83090878
  *((volatile uint32_t*)(base + 0x8213D1C8)) = __builtin_bswap32(2198407288u);  // -> 0x83090878
  *((volatile uint32_t*)(base + 0x8213D370)) = __builtin_bswap32(2198407288u);  // -> 0x83090878
  *((volatile uint32_t*)(base + 0x8213D45C)) = __builtin_bswap32(2198428200u);  // -> 0x83095A28
  *((volatile uint32_t*)(base + 0x8213D0A8)) = __builtin_bswap32(2198428216u);  // -> 0x83095A38
  *((volatile uint32_t*)(base + 0x8213D1D0)) = __builtin_bswap32(2198428216u);  // -> 0x83095A38
  *((volatile uint32_t*)(base + 0x8213D378)) = __builtin_bswap32(2198428216u);  // -> 0x83095A38
  *((volatile uint32_t*)(base + 0x8213D0C0)) = __builtin_bswap32(2198428224u);  // -> 0x83095A40
  *((volatile uint32_t*)(base + 0x8213D1E8)) = __builtin_bswap32(2198428224u);  // -> 0x83095A40
  *((volatile uint32_t*)(base + 0x8213D390)) = __builtin_bswap32(2198428224u);  // -> 0x83095A40
  *((volatile uint32_t*)(base + 0x8213D47C)) = __builtin_bswap32(2198428232u);  // -> 0x83095A48
  *((volatile uint32_t*)(base + 0x8213D4D4)) = __builtin_bswap32(2198433232u);  // -> 0x83096DD0
  *((volatile uint32_t*)(base + 0x8213D440)) = __builtin_bswap32(2198433248u);  // -> 0x83096DE0
  *((volatile uint32_t*)(base + 0x8213D4B8)) = __builtin_bswap32(2198433248u);  // -> 0x83096DE0
  *((volatile uint32_t*)(base + 0x8213D410)) = __builtin_bswap32(2198433256u);  // -> 0x83096DE8
  *((volatile uint32_t*)(base + 0x8213D488)) = __builtin_bswap32(2198433256u);  // -> 0x83096DE8
  *((volatile uint32_t*)(base + 0x8213D0B8)) = __builtin_bswap32(2198433264u);  // -> 0x83096DF0
  *((volatile uint32_t*)(base + 0x8213D1E0)) = __builtin_bswap32(2198433264u);  // -> 0x83096DF0
  *((volatile uint32_t*)(base + 0x8213D388)) = __builtin_bswap32(2198433264u);  // -> 0x83096DF0
  *((volatile uint32_t*)(base + 0x8213D0E0)) = __builtin_bswap32(2198433272u);  // -> 0x83096DF8
  *((volatile uint32_t*)(base + 0x8213D208)) = __builtin_bswap32(2198433272u);  // -> 0x83096DF8
  *((volatile uint32_t*)(base + 0x8213D3B0)) = __builtin_bswap32(2198433272u);  // -> 0x83096DF8
  *((volatile uint32_t*)(base + 0x8213D0B4)) = __builtin_bswap32(2198433280u);  // -> 0x83096E00
  *((volatile uint32_t*)(base + 0x8213D1DC)) = __builtin_bswap32(2198433280u);  // -> 0x83096E00
  *((volatile uint32_t*)(base + 0x8213D384)) = __builtin_bswap32(2198433280u);  // -> 0x83096E00
  *((volatile uint32_t*)(base + 0x8213D4F4)) = __builtin_bswap32(2198433288u);  // -> 0x83096E08
  *((volatile uint32_t*)(base + 0x8213D5FC)) = __builtin_bswap32(2198435272u);  // -> 0x830975C8
  *((volatile uint32_t*)(base + 0x8213D594)) = __builtin_bswap32(2198438648u);  // -> 0x830982F8
  *((volatile uint32_t*)(base + 0x8213D658)) = __builtin_bswap32(2198442032u);  // -> 0x83099030
  *((volatile uint32_t*)(base + 0x8213D834)) = __builtin_bswap32(2198454016u);  // -> 0x8309BF00
  *((volatile uint32_t*)(base + 0x8213D7F4)) = __builtin_bswap32(2198454712u);  // -> 0x8309C1B8
  *((volatile uint32_t*)(base + 0x823B06C0)) = __builtin_bswap32(2198494040u);  // -> 0x830A5B58
  *((volatile uint32_t*)(base + 0x823B06C8)) = __builtin_bswap32(2198494040u);  // -> 0x830A5B58
  *((volatile uint32_t*)(base + 0x823B06E0)) = __builtin_bswap32(2198504808u);  // -> 0x830A8568
  *((volatile uint32_t*)(base + 0x821416FC)) = __builtin_bswap32(2198511120u);  // -> 0x830A9E10
  *((volatile uint32_t*)(base + 0x8214170C)) = __builtin_bswap32(2198511128u);  // -> 0x830A9E18
  *((volatile uint32_t*)(base + 0x821416B0)) = __builtin_bswap32(2198517504u);  // -> 0x830AB700
  *((volatile uint32_t*)(base + 0x821417C8)) = __builtin_bswap32(2198517504u);  // -> 0x830AB700
  *((volatile uint32_t*)(base + 0x8215EE00)) = __builtin_bswap32(2198841848u);  // -> 0x830FA9F8
  *((volatile uint32_t*)(base + 0x8215EE08)) = __builtin_bswap32(2198842304u);  // -> 0x830FABC0
  *((volatile uint32_t*)(base + 0x8215EE40)) = __builtin_bswap32(2198842304u);  // -> 0x830FABC0
  *((volatile uint32_t*)(base + 0x82161C68)) = __builtin_bswap32(2198899184u);  // -> 0x831089F0
  *((volatile uint32_t*)(base + 0x82161C88)) = __builtin_bswap32(2198903872u);  // -> 0x83109C40
  *((volatile uint32_t*)(base + 0x8217E06C)) = __builtin_bswap32(2200014000u);  // -> 0x83218CB0
  *((volatile uint32_t*)(base + 0x8217D514)) = __builtin_bswap32(2200016440u);  // -> 0x83219638
  *((volatile uint32_t*)(base + 0x8217F500)) = __builtin_bswap32(2200210624u);  // -> 0x83248CC0
  *((volatile uint32_t*)(base + 0x8217F514)) = __builtin_bswap32(2200210648u);  // -> 0x83248CD8
  *((volatile uint32_t*)(base + 0x8217F474)) = __builtin_bswap32(2200218224u);  // -> 0x8324AA70
  *((volatile uint32_t*)(base + 0x8217F67C)) = __builtin_bswap32(2200224528u);  // -> 0x8324C310
  *((volatile uint32_t*)(base + 0x8217F848)) = __builtin_bswap32(2200225664u);  // -> 0x8324C780
  *((volatile uint32_t*)(base + 0x82180538)) = __builtin_bswap32(2200225664u);  // -> 0x8324C780
  *((volatile uint32_t*)(base + 0x8217F96C)) = __builtin_bswap32(2200316048u);  // -> 0x83262890
  *((volatile uint32_t*)(base + 0x82191CC0)) = __builtin_bswap32(2200410008u);  // -> 0x83279798
  *((volatile uint32_t*)(base + 0x82192F2C)) = __builtin_bswap32(2200429040u);  // -> 0x8327E1F0
  *((volatile uint32_t*)(base + 0x82193700)) = __builtin_bswap32(2200464688u);  // -> 0x83286D30
  *((volatile uint32_t*)(base + 0x8218C69C)) = __builtin_bswap32(2200633064u);  // -> 0x832AFEE8
  *((volatile uint32_t*)(base + 0x820142D4)) = __builtin_bswap32(2201451416u);  // -> 0x83377B98
  *((volatile uint32_t*)(base + 0x820142DC)) = __builtin_bswap32(2201451432u);  // -> 0x83377BA8
  *((volatile uint32_t*)(base + 0x82014490)) = __builtin_bswap32(2202061672u);  // -> 0x8340CB68
  *((volatile uint32_t*)(base + 0x8223394C)) = __builtin_bswap32(2202230112u);  // -> 0x83435D60
  *((volatile uint32_t*)(base + 0x8222F2C4)) = __builtin_bswap32(2202242424u);  // -> 0x83438D78
  *((volatile uint32_t*)(base + 0x8222F2E4)) = __builtin_bswap32(2202243232u);  // -> 0x834390A0
  *((volatile uint32_t*)(base + 0x8222F2E8)) = __builtin_bswap32(2202243256u);  // -> 0x834390B8
  *((volatile uint32_t*)(base + 0x8222F2EC)) = __builtin_bswap32(2202243280u);  // -> 0x834390D0
  *((volatile uint32_t*)(base + 0x8227A79C)) = __builtin_bswap32(2203137728u);  // -> 0x835136C0
  *((volatile uint32_t*)(base + 0x8227A7A8)) = __builtin_bswap32(2203137752u);  // -> 0x835136D8
  *((volatile uint32_t*)(base + 0x822903A8)) = __builtin_bswap32(2203473216u);  // -> 0x83565540
  *((volatile uint32_t*)(base + 0x822909F4)) = __builtin_bswap32(2203473216u);  // -> 0x83565540
  *((volatile uint32_t*)(base + 0x82290AB4)) = __builtin_bswap32(2203473216u);  // -> 0x83565540
  *((volatile uint32_t*)(base + 0x822CD924)) = __builtin_bswap32(2204259104u);  // -> 0x83625320
  *((volatile uint32_t*)(base + 0x822DF2DC)) = __builtin_bswap32(2204907176u);  // -> 0x836C36A8
  *((volatile uint32_t*)(base + 0x822EE808)) = __builtin_bswap32(2205147576u);  // -> 0x836FE1B8
  *((volatile uint32_t*)(base + 0x822EE818)) = __builtin_bswap32(2205147584u);  // -> 0x836FE1C0
  *((volatile uint32_t*)(base + 0x822EE804)) = __builtin_bswap32(2205147592u);  // -> 0x836FE1C8
  *((volatile uint32_t*)(base + 0x822EE854)) = __builtin_bswap32(2205148280u);  // -> 0x836FE478
  *((volatile uint32_t*)(base + 0x822EE83C)) = __builtin_bswap32(2205148288u);  // -> 0x836FE480
  *((volatile uint32_t*)(base + 0x822EE850)) = __builtin_bswap32(2205148296u);  // -> 0x836FE488
  *((volatile uint32_t*)(base + 0x822EEB00)) = __builtin_bswap32(2205155688u);  // -> 0x83700168
  *((volatile uint32_t*)(base + 0x822EEBD0)) = __builtin_bswap32(2205156928u);  // -> 0x83700640
  *((volatile uint32_t*)(base + 0x823B0758)) = __builtin_bswap32(2205364064u);  // -> 0x83732F60
  *((volatile uint32_t*)(base + 0x823B0760)) = __builtin_bswap32(2205364096u);  // -> 0x83732F80
  *((volatile uint32_t*)(base + 0x823B0780)) = __builtin_bswap32(2205364440u);  // -> 0x837330D8
  *((volatile uint32_t*)(base + 0x823B0798)) = __builtin_bswap32(2205365316u);  // -> 0x83733444
  *((volatile uint32_t*)(base + 0x823B07B0)) = __builtin_bswap32(2205365636u);  // -> 0x83733584
  *((volatile uint32_t*)(base + 0x823B07C8)) = __builtin_bswap32(2205367396u);  // -> 0x83733C64
}
