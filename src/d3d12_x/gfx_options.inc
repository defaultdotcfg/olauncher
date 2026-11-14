// Graphics options as native rows of the game's own Options > GRAPHICS page (no overlay).
//
// The options menu is built in code (exe+0x354E00..0x3559AE): each row is a 0x40-byte item
// {name key, widget type, value, invoker, callback, id, value-key table, value count} appended to the
// page's item vector by exe+0x340DB0. Row and value names are localisation keys looked up by
// exe+0x22DFA0, so new keys ("ID_RL_*") get their text from a hook on that lookup.
//
// Rows (inserted where the console builds add "Visuals" on Xbox One X):
//  * Graphics quality: Console (the Xbox One profile) or the PC profiles the developers shipped in
//    code:graphics/viewquality.cfg (QualityConfig-PC-UltraLow .. -UltraHigh: render scale, shadow
//    maps, LOD bias, tessellation). The game applies a quality profile through exe+0x3DCD60 (profile
//    load, photo mode exit, ...); its base-console names are redirected to the chosen profile, so the
//    choice holds everywhere and is applied at once through the game's own reload.
//  * Frame rate limit (30 .. 240, unlimited): time-based limiter in front of Present, independent
//    of the monitor's refresh rate (the game's FlipRate 2 only meant "every other vblank").
//  * Vertical sync: on = Present waits for vblank; off = tearing allowed (no extra latency).
// Choices persist in the launcher's onrush.ini (ONRUSH_INI) and come back through the environment.

#include <dxgi1_5.h>

namespace gfxopt {

static const char *const kPresetSuffix[] = {"XBoxOne-Base", "PC-UltraLow", "PC-Low", "PC-Medium", "PC-High", "PC-UltraHigh"};
static const int kPresetCount = 6;
static const int kFpsChoices[] = {30, 60, 90, 120, 144, 165, 240, 0};  // 0 = unlimited
static const int kFpsCount = 8;

static volatile LONG g_preset = 0;
static volatile LONG g_fpsIdx = 1;
static volatile LONG g_vsync = 1;
static wchar_t g_ini[MAX_PATH] = {};
static HMODULE g_exe = nullptr;
static bool g_tearingSupported = false;

// Profile names the game reads (near the exe so `lea reg,[rip+x]` can reach them).
static char *g_qcName = nullptr;     // "QualityConfig-<suffix>"
static char *g_qcPhoto = nullptr;    // "QualityConfig-PhotoMode-<suffix>"

static int EnvInt(const char *name, int def) {
  char v[16] = {};
  DWORD n = GetEnvironmentVariableA(name, v, sizeof(v));
  return (n > 0 && n < sizeof(v)) ? atoi(v) : def;
}

static void UpdateProfileNames() {
  if (!g_qcName) return;
  int p = g_preset;
  if (p < 0 || p >= kPresetCount) p = 0;
  _snprintf_s(g_qcName, 64, _TRUNCATE, "QualityConfig-%s", kPresetSuffix[p]);
  _snprintf_s(g_qcPhoto, 64, _TRUNCATE, "QualityConfig-PhotoMode-%s", kPresetSuffix[p]);
}

static void SaveIni() {
  if (!g_ini[0]) return;
  wchar_t b[16];
  swprintf_s(b, L"%ld", (long)g_preset);
  WritePrivateProfileStringW(L"Graphics", L"Preset", b, g_ini);
  swprintf_s(b, L"%d", kFpsChoices[g_fpsIdx]);
  WritePrivateProfileStringW(L"Graphics", L"FpsLimit", b, g_ini);
  swprintf_s(b, L"%ld", (long)g_vsync);
  WritePrivateProfileStringW(L"Display", L"VSync", b, g_ini);
}

// ---- texts --------------------------------------------------------------------------------------
enum { L_EN, L_PT, L_FR, L_ES, L_DE, L_IT, L_PL, L_COUNT };

static int TextLang() {
  static int s_lang = -1;
  if (s_lang >= 0) return s_lang;
  wchar_t loc[LOCALE_NAME_MAX_LENGTH] = {};
  DWORD n = GetEnvironmentVariableW(L"ONRUSH_TEXT_LANG", loc, LOCALE_NAME_MAX_LENGTH);
  if (n == 0 || n >= LOCALE_NAME_MAX_LENGTH) GetUserDefaultLocaleName(loc, LOCALE_NAME_MAX_LENGTH);
  static const struct { const wchar_t *pre; int lang; } map[] = {
    {L"pt", L_PT}, {L"fr", L_FR}, {L"es", L_ES}, {L"de", L_DE}, {L"it", L_IT}, {L"pl", L_PL}};
  s_lang = L_EN;
  for (const auto &m : map)
    if (_wcsnicmp(loc, m.pre, 2) == 0) s_lang = m.lang;
  return s_lang;
}

struct Text { const char *key; const char *t[L_COUNT]; };  // UTF-8, upper case like the game's own; en, pt, fr, es, de, it, pl.
// List rows clip (and scroll) row names past ~17 characters: keep them short.
static const Text kTexts[] = {
  {"ID_RL_GFX_QUALITY", {"GRAPHICS QUALITY", "QUALIDADE GRÁFICA", "QUALITÉ GRAPHIQUE", "CALIDAD GRÁFICA",
                         "GRAFIKQUALITÄT", "QUALITÀ GRAFICA", "JAKOŚĆ GRAFIKI"}},
  {"ID_RL_Q_CONSOLE", {"CONSOLE", "CONSOLE", "CONSOLE", "CONSOLA", "KONSOLE", "CONSOLE", "KONSOLA"}},
  {"ID_RL_Q_ULTRALOW", {"ULTRA LOW", "ULTRA BAIXA", "ULTRA BASSE", "ULTRABAJA", "ULTRANIEDRIG", "ULTRA BASSA", "BARDZO NISKA"}},
  {"ID_RL_Q_LOW", {"LOW", "BAIXA", "BASSE", "BAJA", "NIEDRIG", "BASSA", "NISKA"}},
  {"ID_RL_Q_MEDIUM", {"MEDIUM", "MÉDIA", "MOYENNE", "MEDIA", "MITTEL", "MEDIA", "ŚREDNIA"}},
  {"ID_RL_Q_HIGH", {"HIGH", "ALTA", "HAUTE", "ALTA", "HOCH", "ALTA", "WYSOKA"}},
  {"ID_RL_Q_ULTRA", {"ULTRA", "ULTRA", "ULTRA", "ULTRA", "ULTRA", "ULTRA", "ULTRA"}},
  {"ID_RL_FPS_LIMIT", {"FRAME RATE LIMIT", "LIMITE DE QUADROS", "LIMITE D'IMAGES", "LÍMITE DE FPS",
                       "FPS-LIMIT", "LIMITE FPS", "LIMIT KLATEK"}},
  {"ID_RL_FPS_UNLIMITED", {"UNLIMITED", "SEM LIMITE", "ILLIMITÉ", "SIN LÍMITE", "UNBEGRENZT", "ILLIMITATO", "BEZ LIMITU"}},
  {"ID_RL_VSYNC", {"VERTICAL SYNC", "V-SYNC", "SYNCHRO VERTICALE", "V-SYNC", "V-SYNC", "V-SYNC", "V-SYNC"}},
  {"ID_RL_OFF", {"OFF", "DESLIGADO", "DÉSACTIVÉE", "DESACTIVADO", "AUS", "DISATTIVATO", "WYŁĄCZONE"}},
  {"ID_RL_ON", {"ON", "LIGADO", "ACTIVÉE", "ACTIVADO", "AN", "ATTIVATO", "WŁĄCZONE"}},
};
static const char *const kFpsKeys[] = {"ID_RL_FPS_30", "ID_RL_FPS_60", "ID_RL_FPS_90", "ID_RL_FPS_120",
                                       "ID_RL_FPS_144", "ID_RL_FPS_165", "ID_RL_FPS_240", "ID_RL_FPS_UNLIMITED"};
static const char *const kFpsLabels[] = {"30 FPS", "60 FPS", "90 FPS", "120 FPS", "144 FPS", "165 FPS", "240 FPS"};

typedef const char *(*PFN_Localise)(const char *key);
static PFN_Localise g_trueLocalise = nullptr;

static const char *HookLocalise(const char *key) {
  if (key && key[0] == 'I' && strncmp(key, "ID_RL_", 6) == 0) {
    for (int i = 0; i < 7; ++i)
      if (strcmp(key, kFpsKeys[i]) == 0) return kFpsLabels[i];
    for (const Text &t : kTexts)
      if (strcmp(key, t.key) == 0) return t.t[TextLang()];
  }
  return g_trueLocalise(key);
}

// ---- menu rows ----------------------------------------------------------------------------------
struct MenuItem {           // layout used by exe+0x340DB0 (AddItem) and the builder
  const char *name;         // +0x00 localisation key
  uint32_t type;            // +0x08 widget: 6 = list of `count` values
  float value;              // +0x0C current value (list index)
  void *invoker;            // +0x10 exe+0x35E960: calls `callback(value, ...)`
  void *callback;           // +0x18
  uint64_t unused20;        // +0x20
  uint32_t id;              // +0x28
  uint32_t unused2c;        // +0x2C
  const char *const *values;// +0x30 value keys
  uint32_t count;           // +0x38
  uint32_t unused3c;        // +0x3C
};
static_assert(sizeof(MenuItem) == 0x40, "menu item layout");

static const char *const kPresetKeys[] = {"ID_RL_Q_CONSOLE", "ID_RL_Q_ULTRALOW", "ID_RL_Q_LOW",
                                          "ID_RL_Q_MEDIUM", "ID_RL_Q_HIGH", "ID_RL_Q_ULTRA"};
static const char *const kOnOffKeys[] = {"ID_RL_OFF", "ID_RL_ON"};
enum : uint32_t { kIdQuality = 0x52, kIdFps = 0x53, kIdVsync = 0x54 };

static int ListIndex(float v, int count) {
  int i = (int)(v + 0.5f);
  return i < 0 ? 0 : i >= count ? count - 1 : i;
}

// Re-applies the game's quality profile through its own path (what the Xbox One X "Visuals" row does,
// exe+0x3B42C0): profile owner = exe+0x3CD710(exe+0x3CD100()), apply = exe+0x3DCD60(owner, flag).
static void ApplyQualityProfile() {
  typedef void *(*PFN_Get0)();
  typedef void *(*PFN_Get1)(void *);
  typedef void (*PFN_Apply)(void *, uint8_t);
  void *owner = ((PFN_Get1)((uint8_t *)g_exe + 0x003CD710))(((PFN_Get0)((uint8_t *)g_exe + 0x003CD100))());
  if (owner) ((PFN_Apply)((uint8_t *)g_exe + 0x003DCD60))(owner, *((uint8_t *)owner + 0x20));
}

static void OnQuality(float value, int) {
  LONG p = ListIndex(value, kPresetCount);
  if (p == g_preset) return;
  g_preset = p;
  UpdateProfileNames();
  ApplyQualityProfile();
  SaveIni();
  log_line("[gfx] graphics quality -> %s\r\n", g_qcName);
}
static void OnFps(float value, int) {
  g_fpsIdx = ListIndex(value, kFpsCount);
  SaveIni();
  log_line("[gfx] frame rate limit -> %d\r\n", kFpsChoices[g_fpsIdx]);
}
static void OnVsync(float value, int) {
  g_vsync = ListIndex(value, 2);
  SaveIni();
  log_line("[gfx] vertical sync -> %ld\r\n", (long)g_vsync);
}

static void AddRows(void *page) {
  typedef void (*PFN_AddItem)(void *page, const MenuItem *item);
  PFN_AddItem add = (PFN_AddItem)((uint8_t *)g_exe + 0x00340DB0);
  void *invoker = (uint8_t *)g_exe + 0x0035E960;
  MenuItem it = {};
  it.type = 6;
  it.invoker = invoker;

  it.name = "ID_RL_GFX_QUALITY"; it.callback = (void *)&OnQuality; it.id = kIdQuality;
  it.values = kPresetKeys; it.count = kPresetCount; it.value = (float)g_preset;
  add(page, &it);
  it.name = "ID_RL_FPS_LIMIT"; it.callback = (void *)&OnFps; it.id = kIdFps;
  it.values = kFpsKeys; it.count = kFpsCount; it.value = (float)g_fpsIdx;
  add(page, &it);
  it.name = "ID_RL_VSYNC"; it.callback = (void *)&OnVsync; it.id = kIdVsync;
  it.values = kOnOffKeys; it.count = 2; it.value = (float)g_vsync;
  add(page, &it);
  static volatile LONG s_n = 0;
  log_line("[gfx] graphics rows added to the options page (build #%ld)\r\n", InterlockedIncrement(&s_n));
}

// ---- patching helpers ---------------------------------------------------------------------------
static bool WriteCode(uint8_t *p, const void *src, size_t n) {
  DWORD old = 0;
  if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old)) return false;
  memcpy(p, src, n);
  VirtualProtect(p, n, old, &old);
  FlushInstructionCache(GetCurrentProcess(), p, n);
  return true;
}

// `lea reg, [rip+disp32]` (7 bytes) at rva -> points at `target` instead of `expect`.
static bool RetargetLea(uint32_t rva, const char *expect, const void *target) {
  uint8_t *p = (uint8_t *)g_exe + rva;
  if ((p[0] != 0x48 && p[0] != 0x4C) || p[1] != 0x8D || (p[2] & 0xC7) != 0x05) return false;
  const char *cur = (const char *)(p + 7 + *(int32_t *)(p + 3));
  if (strcmp(cur, expect) != 0) return false;
  int64_t d = (int64_t)((const uint8_t *)target - (p + 7));
  if (d < INT32_MIN || d > INT32_MAX) return false;
  int32_t d32 = (int32_t)d;
  return WriteCode(p + 3, &d32, 4);
}

// Stub: mov rax, imm64 ; jmp rax
static uint8_t *EmitJmpAbs(uint8_t *q, const void *to) {
  *q++ = 0x48; *q++ = 0xB8; memcpy(q, &to, 8); q += 8;
  *q++ = 0xFF; *q++ = 0xE0;
  return q;
}

static bool WriteJmpRel(uint8_t *site, const uint8_t *to, size_t patchLen) {
  int64_t d = (int64_t)(to - (site + 5));
  if (d < INT32_MIN || d > INT32_MAX) return false;
  uint8_t b[16];
  b[0] = 0xE9; int32_t d32 = (int32_t)d; memcpy(b + 1, &d32, 4);
  for (size_t i = 5; i < patchLen; ++i) b[i] = 0x90;
  return WriteCode(site, b, patchLen);
}

static void Install(HMODULE hExe) {
  g_exe = hExe;
  {
    wchar_t ini[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"ONRUSH_INI", ini, MAX_PATH);
    if (n > 0 && n < MAX_PATH) wcscpy_s(g_ini, ini);
  }
  int p = EnvInt("ONRUSH_GFX_PRESET", 0);
  g_preset = (p >= 0 && p < kPresetCount) ? p : 0;
  int fps = EnvInt("ONRUSH_FPS_LIMIT", 60);
  g_fpsIdx = 1;
  for (int i = 0; i < kFpsCount; ++i)
    if (kFpsChoices[i] == fps) g_fpsIdx = i;
  g_vsync = EnvInt("ONRUSH_VSYNC", 1) ? 1 : 0;

  uint8_t *exe = (uint8_t *)hExe;
  uint8_t *mem = (uint8_t *)AllocateNearAddress(exe + 0x00300000, 0x1000);
  if (!mem) { log_line("[gfx] WARNING: no memory near the exe; graphics rows not installed\r\n"); return; }
  DWORD old = 0;
  VirtualProtect(mem, 0x1000, PAGE_EXECUTE_READWRITE, &old);
  g_qcName = (char *)mem; g_qcPhoto = (char *)mem + 128;
  UpdateProfileNames();
  uint8_t *code = mem + 256;

  // 1. Quality profile names: exe+0x3DCD60 (base console: quality / 60 Hz) and photo mode entry.
  //    "QualityConfig-XBoxOne-Base-60Hz" is not in viewquality.cfg: the redirect also stops the
  //    base console from asking for a profile that does not exist. The boot profile (table at
  //    exe+0xEE1428) stays the console's: the shadow map sizes are only read at boot, and the PC
  //    profiles' 8192x8192 shadow map does not fit the boot-time page list (exe+0xB5C78). The chosen
  //    profile is applied when the player profile loads (exe+0x384381 -> 0x3DCD60), like a menu change.
  bool okQ = RetargetLea(0x003DCD86, "QualityConfig-XBoxOne-Base", g_qcName) &&
             RetargetLea(0x003DCD8D, "QualityConfig-XBoxOne-Base-60Hz", g_qcName);
  bool okP = RetargetLea(0x00366FDE, "QualityConfig-PhotoMode-XBoxOne-Base", g_qcPhoto);
  log_line("[gfx] quality profile -> %s (apply %d, photo %d)\r\n", g_qcName, okQ, okP);

  // 2. Localisation lookup exe+0x22DFA0: `mov [rsp+8],rbx ; mov [rsp+18h],rsi` (10 bytes) moved to a
  //    trampoline, entry jumps to HookLocalise.
  uint8_t *loc = exe + 0x0022DFA0;
  static const uint8_t kLocHead[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
  // 3. Options builder exe+0x3550D6: `cmp byte [Rendering/FPSVsQualityOption], r13b ; ...` -> the
  //    Visuals block (up to 0x35514C) is replaced by AddRows(rbx = display page). r15 is set to the
  //    Visuals callback as the skipped block did (the second half of the builder reads it).
  uint8_t *bld = exe + 0x003550D6;
  static const uint8_t kBldHead[] = {0x44, 0x38, 0x2D};
  if (memcmp(loc, kLocHead, sizeof(kLocHead)) != 0 || memcmp(bld, kBldHead, sizeof(kBldHead)) != 0 ||
      memcmp(exe + 0x0035514C, "\x48\x8B\x53\x10", 4) != 0) {
    log_line("[gfx] WARNING: unexpected code at exe+0x22DFA0/0x3550D6; graphics rows not installed\r\n");
    return;
  }
  uint8_t *q = code;
  g_trueLocalise = (PFN_Localise)q;                 // trampoline: original 10 bytes, back to +10
  memcpy(q, loc, 10); q += 10;
  q = EmitJmpAbs(q, loc + 10);
  uint8_t *locStub = q;                             // entry stub -> HookLocalise
  q = EmitJmpAbs(q, (void *)&HookLocalise);
  uint8_t *bldStub = q;
  *q++ = 0x48; *q++ = 0x89; *q++ = 0xD9;            // mov rcx, rbx
  *q++ = 0x48; *q++ = 0x83; *q++ = 0xEC; *q++ = 0x20; // sub rsp, 20h (rsp stays as at the game's own calls)
  *q++ = 0x48; *q++ = 0xB8; { void *f = (void *)&AddRows; memcpy(q, &f, 8); q += 8; }  // mov rax, AddRows
  *q++ = 0xFF; *q++ = 0xD0;                         // call rax
  *q++ = 0x48; *q++ = 0x83; *q++ = 0xC4; *q++ = 0x20; // add rsp, 20h
  *q++ = 0x49; *q++ = 0xBF; { void *cb = exe + 0x003B42C0; memcpy(q, &cb, 8); q += 8; }  // mov r15, Visuals cb
  q = EmitJmpAbs(q, exe + 0x0035514C);
  FlushInstructionCache(GetCurrentProcess(), code, (size_t)(q - code));

  bool okL = WriteJmpRel(loc, locStub, 5);
  bool okB = okL && WriteJmpRel(bld, bldStub, 7);
  log_line("[gfx] graphics rows %s (texts %d, builder %d); fps limit %d, vsync %ld\r\n",
           okB ? "installed" : "NOT installed", okL, okB, kFpsChoices[g_fpsIdx], (long)g_vsync);
}

// ---- presentation -------------------------------------------------------------------------------
static void DetectTearing(IDXGIFactory2 *f) {
  IDXGIFactory5 *f5 = nullptr;
  BOOL allow = FALSE;
  if (f && SUCCEEDED(f->QueryInterface(__uuidof(IDXGIFactory5), (void **)&f5)) && f5) {
    if (FAILED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow)))) allow = FALSE;
    f5->Release();
  }
  g_tearingSupported = allow != FALSE;
}
static bool TearingFlag(IDXGIFactory2 *f) { DetectTearing(f); return g_tearingSupported; }

static double MonitorHz(HWND hwnd) {
  static double s_hz = 60.0;
  static ULONGLONG s_t = 0;
  ULONGLONG now = GetTickCount64();
  if (s_t && now - s_t < 2000) return s_hz;
  s_t = now;
  MONITORINFOEXW mi = {};
  mi.cbSize = sizeof(mi);
  DEVMODEW dm = {};
  dm.dmSize = sizeof(dm);
  HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
  if (mon && GetMonitorInfoW(mon, &mi) && EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) &&
      dm.dmDisplayFrequency > 1)
    s_hz = dm.dmDisplayFrequency == 59 ? 59.94 : (double)dm.dmDisplayFrequency;
  return s_hz;
}

// Waits until the next frame slot of a `fps` cap (high-resolution timer, then a short spin).
static void Limit(int fps) {
  static LARGE_INTEGER s_freq = {};
  static LONGLONG s_next = 0;
  static HANDLE s_timer = nullptr;
  if (!s_freq.QuadPart) {
    QueryPerformanceFrequency(&s_freq);
    s_timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  }
  LONGLONG period = s_freq.QuadPart / fps;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  if (!s_next || now.QuadPart - s_next > period * 4 || s_next - now.QuadPart > period * 4) s_next = now.QuadPart;
  LONGLONG wait = s_next - now.QuadPart;
  LONGLONG spin = s_freq.QuadPart / 1000;  // last ~1 ms spins
  if (wait > spin && s_timer) {
    LARGE_INTEGER due;
    due.QuadPart = -((wait - spin) * 10000000LL / s_freq.QuadPart);
    if (SetWaitableTimer(s_timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(s_timer, 100);
  }
  do { QueryPerformanceCounter(&now); YieldProcessor(); } while (now.QuadPart < s_next);
  s_next += period;
}

// Sync interval and present flags for this frame; runs the frame limiter when needed.
static UINT PresentSync(HWND hwnd, UINT *flags) {
  int cap = kFpsChoices[g_fpsIdx];
  if (g_vsync) {
    double hz = MonitorHz(hwnd);
    if (cap == 0 || cap >= hz * 0.97) return 1;
    int n = (int)(hz / cap + 0.5);
    if (n >= 1 && n <= 4 && fabs(hz / n - cap) <= cap * 0.03) return (UINT)n;  // whole vblanks: smoothest
    Limit(cap);
    return 1;
  }
  if (cap) Limit(cap);
  if (g_tearingSupported) *flags |= DXGI_PRESENT_ALLOW_TEARING;
  return 0;
}

}  // namespace gfxopt
