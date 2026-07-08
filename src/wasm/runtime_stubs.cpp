// WASM minimal runtime stubs — provides just enough symbol definitions
// for the generated recomp to link and boot the guest entry point.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <mutex>
#include <unordered_map>

#include <emscripten.h>

#include <rex/types.h>
#include <rex/ppc.h>
#include <rex/image_info.h>
#include <rex/logging.h>
#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/thread/mutex.h>
#include <rex/system/mmio_handler.h>

// ===========================================================================
// Guest memory
// ===========================================================================

static uint8_t g_guest_base[64 * 1024 * 1024]; // 64 MB
static std::atomic<bool> g_ready{false};

static uint8_t* get_base() {
  if (!g_ready.exchange(true)) {
    std::fprintf(stderr, "[wasm] guest mem: 64MB @ %p\n", (void*)g_guest_base);
  }
  return g_guest_base;
}

// ===========================================================================
// Function dispatch
// ===========================================================================

// PPCFunc is defined in rex/ppc/context.h:
//   using PPCFunc = void(PPCContext& ctx, uint8_t* base);
static std::unordered_map<uint32_t, ::PPCFunc*> g_funcs;
static bool g_funcs_ready = false;

static void build_funcs() {
  if (g_funcs_ready) return;
  for (auto* p = PPCFuncMappings; p->guest != 0; ++p)
    g_funcs[(uint32_t)p->guest] = p->host;
  g_funcs_ready = true;
  std::fprintf(stderr, "[wasm] func table: %zu entries\n", g_funcs.size());
}

namespace rex::runtime {
::PPCFunc* ResolveIndirectFunction(uint32_t addr) {
  build_funcs();
  auto it = g_funcs.find(addr);
  if (it != g_funcs.end()) return it->second;
  std::fprintf(stderr, "[wasm] unresolved indirect: 0x%08X\n", addr);
  return nullptr;
}
}

// ===========================================================================
// Clock
// ===========================================================================

namespace rex::chrono {
uint64_t Clock::QueryGuestTickCount() { return (uint64_t)(emscripten_get_now() * 50000.0); }
uint64_t Clock::QueryGuestSystemTime() { return (uint64_t)(emscripten_get_now() * 10000.0); }
uint64_t Clock::QueryHostSystemTime()  { return (uint64_t)(emscripten_get_now() * 10000.0); }
double Clock::guest_time_scalar() { return 1.0; }
}

// ===========================================================================
// Logging
// ===========================================================================

namespace rex {
void InitLoggingEarly() {}
void ShutdownLogging() {}
void SetAllLevels(spdlog::level::level_enum) {}
}

// ===========================================================================
// CVAR
// ===========================================================================

namespace rex::cvar {
std::vector<std::string> Init(int, char**) { return {}; }
void ApplyEnvironment() {}
}

// ===========================================================================
// Thread — set_name is defined inline in the header, nothing needed here.
// ===========================================================================

// ===========================================================================
// global_critical_region::mutex()
// ===========================================================================

static std::recursive_mutex g_critsec;

namespace rex::thread {
std::recursive_mutex& global_critical_region::mutex() { return g_critsec; }
}

// ===========================================================================
// MMIO handler — return nullptr; guest crashes here = success (it ran that far!)
// ===========================================================================

namespace rex::runtime {
MMIOHandler* MMIOHandler::global_handler() { return nullptr; }
MMIOHandler::~MMIOHandler() = default;
}

// ===========================================================================
// PPCImageConfig
// ===========================================================================

extern "C" {
const rex::PPCImageInfo PPCImageConfig = {
  /*code_base  =*/ 0x82450000,
  /*code_size  =*/ 0x153E530,
  /*image_base =*/ 0x82000000,
  /*image_size =*/ 0x1EA0000,
  /*func_mappings =*/ PPCFuncMappings,
  /*rexcrt_heap    =*/ false,
  /*register_modules =*/ nullptr,
};
}

// ===========================================================================
// main()
// ===========================================================================

int main() {
  std::fprintf(stderr, "[wasm] NHL Legacy WASM boot\n");

  auto* base = get_base();
  build_funcs();

  auto* entry = rex::runtime::ResolveIndirectFunction(0x82450000);
  if (!entry) {
    std::fprintf(stderr, "[wasm] FATAL: entry not found\n");
    return 1;
  }

  PPCContext ctx{};
  std::memset(&ctx, 0, sizeof(ctx));
  ctx.r1.u64 = 0x100000000ull - 0x10000ull;
  ctx.r13.u64 = 0x10000000ull;
  ctx.fpscr.InitHost();

  std::fprintf(stderr, "[wasm] calling entry 0x82450000\n");
  entry(ctx, base);
  std::fprintf(stderr, "[wasm] entry returned r3=0x%llX\n", (unsigned long long)ctx.r3.u64);

  return 0;
}
