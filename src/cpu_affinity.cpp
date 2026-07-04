// cpu_affinity.cpp - Intel hybrid (P/E-core) thread placement.
// Windows implementation pins P-cores via SetProcessDefaultCpuSets.
// Linux: no-op stub (scheduling left to the OS).

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <vector>
#else
#include <strings.h>
#endif

void rex_pin_pcores_if_hybrid();

namespace {

bool env_disabled() {
  const char* v = std::getenv("NHL_PIN_PCORES");
  if (!v || !*v) return false;
#ifdef _WIN32
  return std::strcmp(v, "0") == 0 || _stricmp(v, "off") == 0 ||
         _stricmp(v, "false") == 0 || _stricmp(v, "no") == 0;
#else
  auto ieq = [&](const char* s) {
    return ::strcasecmp(v, s) == 0;
  };
  return std::strcmp(v, "0") == 0 || ieq("off") || ieq("false") || ieq("no");
#endif
}

}  // namespace

#ifdef _WIN32

void rex_pin_pcores_if_hybrid() {
  if (env_disabled()) {
    std::fprintf(stderr, "[nhl-affinity] disabled via NHL_PIN_PCORES\n");
    return;
  }

  const HANDLE proc = GetCurrentProcess();
  ULONG bytes = 0;
  GetSystemCpuSetInformation(nullptr, 0, &bytes, proc, 0);
  if (bytes == 0) return;

  std::vector<unsigned char> buf(bytes);
  if (!GetSystemCpuSetInformation(
          reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buf.data()), bytes,
          &bytes, proc, 0)) {
    return;
  }

  BYTE max_eff = 0;
  bool multiple_classes = false;
  BYTE first_eff = 0;
  bool first_seen = false;
  std::vector<ULONG> pcore_ids;

  auto walk = [&](auto visitor) {
    ULONG off = 0;
    while (off + sizeof(SYSTEM_CPU_SET_INFORMATION) <= bytes) {
      auto* info = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buf.data() + off);
      if (info->Size == 0) break;
      if (info->Type == CpuSetInformation) visitor(info->CpuSet);
      off += info->Size;
    }
  };

  walk([&](const auto& cs) {
    if (!first_seen) { first_eff = cs.EfficiencyClass; first_seen = true; }
    else if (cs.EfficiencyClass != first_eff) { multiple_classes = true; }
    if (cs.EfficiencyClass > max_eff) max_eff = cs.EfficiencyClass;
  });

  if (!first_seen) return;
  if (!multiple_classes) {
    std::fprintf(stderr, "[nhl-affinity] non-hybrid CPU; no P-core pinning\n");
    return;
  }

  walk([&](const auto& cs) {
    if (cs.EfficiencyClass == max_eff) pcore_ids.push_back(cs.Id);
  });

  if (pcore_ids.empty()) return;

  if (SetProcessDefaultCpuSets(proc, pcore_ids.data(),
                               static_cast<ULONG>(pcore_ids.size()))) {
    std::fprintf(stderr,
                 "[nhl-affinity] hybrid CPU: pinned process default to %zu "
                 "P-cores (EfficiencyClass %u)\n",
                 pcore_ids.size(), static_cast<unsigned>(max_eff));
  } else {
    std::fprintf(stderr,
                 "[nhl-affinity] SetProcessDefaultCpuSets failed (err %lu)\n",
                 GetLastError());
  }
}

#else

void rex_pin_pcores_if_hybrid() {
  if (env_disabled()) {
    std::fprintf(stderr, "[nhl-affinity] disabled via NHL_PIN_PCORES\n");
    return;
  }
  std::fprintf(stderr, "[nhl-affinity] P-core pinning is Windows-only; no-op on Linux\n");
}

#endif
