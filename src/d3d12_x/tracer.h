#pragma once
// ---------------------------------------------------------------------------
// Tiny function-entry tracer for reverse engineering.
//   ONRUSH_TRACE="1D9300,2D4A30"   RVAs (hex) inside game_release_xbox.exe
//   ONRUSH_TRACE_MAX=40            max logged hits per RVA
// Each listed entry gets an int3. The vectored handler logs registers (plus ASCII
// strings pointed to by rcx/rdx/r8/r9), restores the original byte, single-steps one
// instruction and re-arms the breakpoint. Requires log_line() to be declared.
// ---------------------------------------------------------------------------
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct TraceSite { uint8_t *addr; uint8_t orig; uint32_t rva; volatile LONG hits; };
static TraceSite g_traceSites[64];
static int g_traceCount = 0;
static LONG g_traceMax = 40;
static thread_local TraceSite *t_traceRearm = nullptr;

static void TraceWriteByte(uint8_t *addr, uint8_t v) {
  DWORD old = 0;
  if (VirtualProtect(addr, 1, PAGE_EXECUTE_READWRITE, &old)) {
    *addr = v;
    FlushInstructionCache(GetCurrentProcess(), addr, 1);
    VirtualProtect(addr, 1, old, &old);
  }
}

// ONRUSH_WATCH="RVA:off" (hex): when the traced site RVA is hit, arm a hardware write watchpoint
// (DR0, 8 bytes) on [RSP+off] for that thread; every write logs the writer (exe-relative) and value.
static uint32_t g_watchRva = 0; static uint32_t g_watchOff = 0; static volatile LONG g_watchHits = 0;
static void InstallWatchEnv() {
  char w[64] = {};
  if (GetEnvironmentVariableA("ONRUSH_WATCH", w, sizeof(w)) && w[0]) {
    char *colon = strchr(w, ':');
    g_watchRva = (uint32_t)strtoul(w, nullptr, 16);
    g_watchOff = colon ? (uint32_t)strtoul(colon + 1, nullptr, 16) : 0;
    log_line("[watch] on exe+0x%X -> [rsp+0x%X]\r\n", g_watchRva, g_watchOff);
  }
}

static void InstallTracer(HMODULE hExe) {
  InstallWatchEnv();
  char env[1024] = {};
  if (!GetEnvironmentVariableA("ONRUSH_TRACE", env, sizeof(env)) || !env[0]) return;
  char mx[16] = {};
  if (GetEnvironmentVariableA("ONRUSH_TRACE_MAX", mx, sizeof(mx)) && mx[0]) g_traceMax = atol(mx);
  char *ctx = nullptr;
  for (char *tok = strtok_s(env, ",; ", &ctx); tok && g_traceCount < 64; tok = strtok_s(nullptr, ",; ", &ctx)) {
    uint32_t rva = (uint32_t)strtoul(tok, nullptr, 16);
    if (!rva) continue;
    TraceSite &t = g_traceSites[g_traceCount++];
    t.rva = rva; t.addr = (uint8_t *)hExe + rva; t.orig = *t.addr; t.hits = 0;
    TraceWriteByte(t.addr, 0xCC);
    log_line("[trace] armed exe+0x%X (orig byte %02X)\r\n", rva, t.orig);
  }
}

static bool TraceAscii(const void *p, char *out, size_t n) {
  out[0] = 0;
  uintptr_t v = (uintptr_t)p;
  if (v < 0x10000 || v > 0x00007FFFFFFE0000ULL) return false;
  __try {
    const char *c = (const char *)p; size_t i = 0;
    if (c[0] >= 32 && c[0] < 127 && c[1] == 0 && c[2] >= 32 && c[2] < 127 && c[3] == 0) {  // UTF-16
      const wchar_t *w = (const wchar_t *)p;
      for (; i < n - 1 && w[i] >= 32 && w[i] < 127; ++i) out[i] = (char)w[i];
      out[i] = 0;
      return i >= 3;
    }
    for (; i < n - 1 && c[i] >= 32 && c[i] < 127; ++i) out[i] = c[i];
    out[i] = 0;
    return i >= 3;
  } __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; return false; }
}

// Returns true when the exception was a tracer breakpoint / single-step and was handled.
static bool HandleTraceException(PEXCEPTION_POINTERS pEx) {
  if (!g_traceCount) return false;
  DWORD code = pEx->ExceptionRecord->ExceptionCode;
  PCONTEXT c = pEx->ContextRecord;
  if (code == 0x80000003) {  // int3
    uint8_t *ip = (uint8_t *)c->Rip;
    for (int i = 0; i < g_traceCount; ++i) {
      TraceSite &t = g_traceSites[i];
      if (t.addr != ip) continue;
      LONG n = InterlockedIncrement(&t.hits);
      if (n <= g_traceMax) {
        char s1[80], s2[80], s3[80], s4[80];
        TraceAscii((void *)c->Rcx, s1, sizeof(s1)); TraceAscii((void *)c->Rdx, s2, sizeof(s2));
        TraceAscii((void *)c->R8, s3, sizeof(s3)); TraceAscii((void *)c->R9, s4, sizeof(s4));
        uint64_t ret = 0;
        __try { ret = *(uint64_t *)c->Rsp; } __except (EXCEPTION_EXECUTE_HANDLER) {}
        uint64_t hx = (uint64_t)GetModuleHandleW(nullptr);
        log_line("[trace] exe+0x%X #%ld tid=%lu rax=%llX rcx=%llX rdx=%llX r8=%llX r9=%llX ret=exe+0x%llX s(rcx)='%s' s(rdx)='%s' s(r8)='%s' s(r9)='%s'\r\n",
                 t.rva, (long)n, GetCurrentThreadId(), (unsigned long long)c->Rax, (unsigned long long)c->Rcx, (unsigned long long)c->Rdx,
                 (unsigned long long)c->R8, (unsigned long long)c->R9, (unsigned long long)(ret - hx), s1, s2, s3, s4);
      }
      if (n <= g_traceMax) {
        // extra: callee-saved registers and the first qwords of what rbx/rsi/rdi/r14/r15 point to
        const uint64_t regs[6] = { c->Rbx, c->Rsi, c->Rdi, c->R12, c->R14, c->R15 };
        const char *nm[6] = { "rbx", "rsi", "rdi", "r12", "r14", "r15" };
        for (int ri = 0; ri < 6; ++ri) {
          char dump[300]; int o = _snprintf_s(dump, sizeof(dump), _TRUNCATE, "[trace]   %s=%llX", nm[ri], (unsigned long long)regs[ri]);
          uintptr_t v = (uintptr_t)regs[ri];
          if (v > 0x10000 && v < 0x00007FFFFFFE0000ULL) {
            __try { for (int q = 0; q < 8 && o < 280; ++q) o += _snprintf_s(dump + o, sizeof(dump) - o, _TRUNCATE, " %llX", (unsigned long long)((uint64_t *)v)[q]); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
          }
          log_line("%s\r\n", dump);
        }
      }
      if (n <= g_traceMax && GetEnvironmentVariableA("ONRUSH_TRACE_DEEP", nullptr, 0) > 0) {
        // two-level dump of what rcx points to (find shader bytecode pointers inside an object)
        uintptr_t v = (uintptr_t)c->Rcx;
        if (v > 0x10000 && v < 0x00007FFFFFFE0000ULL) {
          for (int q = 0; q < 24; ++q) {
            uint64_t p = 0; __try { p = ((uint64_t *)v)[q]; } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
            char dump[300]; int o = _snprintf_s(dump, sizeof(dump), _TRUNCATE, "[trace]   rcx[%d]=%llX ->", q, (unsigned long long)p);
            if (p > 0x10000 && p < 0x00007FFFFFFE0000ULL) {
              __try { for (int k = 0; k < 6 && o < 280; ++k) o += _snprintf_s(dump + o, sizeof(dump) - o, _TRUNCATE, " %llX", (unsigned long long)((uint64_t *)p)[k]); }
              __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            log_line("%s\r\n", dump);
          }
        }
      }
      if (g_watchRva && t.rva == g_watchRva) {
        c->Dr0 = c->Rsp + g_watchOff;
        c->Dr7 = (c->Dr7 & ~0xF0003ull) | 0x1ull | (0x1ull << 16) | (0x3ull << 18);  // L0, write, 8 bytes
        log_line("[watch] armed DR0=%llX (tid=%lu)\r\n", (unsigned long long)c->Dr0, GetCurrentThreadId());
      }
      TraceWriteByte(t.addr, t.orig);
      c->EFlags |= 0x100;  // trap flag: single-step
      t_traceRearm = &t;
      return true;
    }
  }
  if (code == 0x80000004 && (c->Dr6 & 1) && g_watchRva) {  // hardware watchpoint hit
    LONG n = InterlockedIncrement(&g_watchHits);
    static LONG s_watchMax = -1;
    if (s_watchMax < 0) { char m[16] = {}; s_watchMax = GetEnvironmentVariableA("ONRUSH_WATCH_MAX", m, sizeof(m)) ? atol(m) : 60; }
    if (n <= s_watchMax) {
      uint64_t hx = (uint64_t)GetModuleHandleW(nullptr), val = 0;
      __try { val = *(uint64_t *)c->Dr0; } __except (EXCEPTION_EXECUTE_HANDLER) {}
      HMODULE hm = nullptr; char mn[64] = "?";
      if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             (LPCSTR)c->Rip, &hm) && hm) {
        char full[MAX_PATH] = {}; GetModuleFileNameA(hm, full, MAX_PATH);
        const char *b = strrchr(full, '\\'); strncpy_s(mn, b ? b + 1 : full, _TRUNCATE);
      }
      char stk[1600]; int so = 0;  // exe-relative return-address-looking values on the stack
      for (int q = 0; q < 512 && so < 1500; ++q) {
        uint64_t v = 0; __try { v = ((uint64_t *)c->Rsp)[q]; } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
        if (v > hx + 0x1000 && v < hx + 0xE90000) so += _snprintf_s(stk + so, sizeof(stk) - so, _TRUNCATE, " %llX", (unsigned long long)(v - hx));
      }
      stk[so] = 0;
      // ONRUSH_WATCH_FILTER=<hex rva>: only report writes whose stack holds this return address
      static char s_filt[24] = {}; static int s_filtInit = 0;
      if (!s_filtInit) { s_filtInit = 1; char f[16] = {};
        if (GetEnvironmentVariableA("ONRUSH_WATCH_FILTER", f, sizeof(f)) && f[0]) sprintf_s(s_filt, " %s", f); }
      if (s_filt[0] && !strstr(stk, s_filt)) InterlockedDecrement(&g_watchHits);
      else log_line("[watch] #%ld write to %llX by %s+0x%llX -> value=%llX (tid=%lu) stack(exe):%s\r\n", (long)n,
               (unsigned long long)c->Dr0, mn, (unsigned long long)(c->Rip - (uint64_t)hm), (unsigned long long)val,
               GetCurrentThreadId(), stk);
    }
    c->Dr6 = 0;
    if (!t_traceRearm) return true;
  }
  if (code == 0x80000004 && t_traceRearm) {  // single-step finished -> re-arm
    TraceWriteByte(t_traceRearm->addr, 0xCC);
    t_traceRearm = nullptr;
    c->EFlags &= ~0x100u;
    return true;
  }
  return false;
}
