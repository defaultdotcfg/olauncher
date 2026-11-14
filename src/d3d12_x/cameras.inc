// In-race camera cycle with the two cameras the shipped game builds but never offers: hood and
// cockpit (first person).
//
// VehicleCamerasMode (ctor ends at exe+0x3FC7DF) keeps its cameras in a list at +0x28
// ({+8 data, +0x10 byte size, +0x30 current index}): 0 = chase far, 1 = chase near, 2 = chase (unused),
// 3 = BonnetCamera, 4 = InteriorCamera. In game the BonnetCamera template sits at the driver's eye
// (roll cage and dashboard in view) and the InteriorCamera one low on the front of the car, so they
// are offered as "cockpit" (3) and "hood" (4). The game itself only toggles 0 <-> 1:
//   - DrivingInput "change_camera" (RB, exe+0x409B9D): flips the profile flag camera_is_near (+0x24 of
//     exe+0x3CD710(exe+0x3CD100())) and sets index = flag (exe+0x1AAEF0).
//   - ctor tail (exe+0x3FC7B1): starts on index 1 if camera_is_near.
// Both sites are redirected to C++: the button cycles far -> near -> hood -> cockpit and a new race
// starts on the last camera used (onrush.ini [Game] Camera; camera_is_near stays in sync for far/near).
namespace cams {

static const uint32_t kCycle[] = {0, 1, 4, 3};
static const char *const kName[] = {"far", "near", "chase", "cockpit", "hood"};
static volatile LONG g_choice = -1;  // last camera index picked (-1 = game default)

static uint8_t *ProfileFlags() {
  typedef void *(*PFN_Get0)();
  typedef uint8_t *(*PFN_Get1)(void *);
  uint8_t *exe = (uint8_t *)gfxopt::g_exe;
  return ((PFN_Get1)(exe + 0x003CD710))(((PFN_Get0)(exe + 0x003CD100))());
}

static uint32_t CameraCount(uint8_t *mode) {
  return (uint32_t)(*(uint64_t *)(mode + 0x28 + 0x10) / 8);
}

static void SaveChoice(uint32_t idx) {
  InterlockedExchange(&g_choice, (LONG)idx);
  if (!gfxopt::g_ini[0]) return;
  wchar_t b[8];
  swprintf_s(b, L"%u", idx);
  WritePrivateProfileStringW(L"Game", L"Camera", b, gfxopt::g_ini);
}

// rcx = VehicleCamerasMode (DrivingInput's owner at exe+0x409B9D).
static void __cdecl OnChangeCamera(uint8_t *mode) {
  uint32_t *cur = (uint32_t *)(mode + 0x28 + 0x30);
  uint32_t count = CameraCount(mode);
  size_t pos = 0;
  for (size_t i = 0; i < _countof(kCycle); ++i)
    if (kCycle[i] == *cur) pos = i;
  uint32_t next = 0;
  for (size_t step = 1; step <= _countof(kCycle); ++step) {
    next = kCycle[(pos + step) % _countof(kCycle)];
    if (next < count) break;
  }
  if (next < 2) {
    uint8_t *prof = ProfileFlags();
    if (prof) prof[0x24] = (uint8_t)next;
  }
  *cur = next;
  SaveChoice(next);
  log_line("[camera] -> %s\r\n", kName[next]);
}

// rcx = new VehicleCamerasMode (end of its ctor, exe+0x3FC7B1).
static void __cdecl OnCamerasModeCreated(uint8_t *mode) {
  uint8_t *prof = ProfileFlags();
  LONG c = g_choice;
  uint32_t idx = (c == 0 || c == 1 || c == 3 || c == 4) ? (uint32_t)c : (prof && prof[0x24]) ? 1u : 0u;
  if (idx >= CameraCount(mode)) idx = 0;
  *(uint32_t *)(mode + 0x28 + 0x30) = idx;
}

// sub rsp,20h ; mov rcx,<reg> ; mov rax,fn ; call rax ; add rsp,20h ; jmp back
static uint8_t *EmitCallStub(uint8_t *q, uint8_t movRcx2, const void *fn, const void *back) {
  *q++ = 0x48; *q++ = 0x83; *q++ = 0xEC; *q++ = 0x20;
  *q++ = 0x48; *q++ = 0x89; *q++ = movRcx2;  // mov rcx, rbx (D9) / rdi (F9)
  *q++ = 0x48; *q++ = 0xB8; memcpy(q, &fn, 8); q += 8;
  *q++ = 0xFF; *q++ = 0xD0;
  *q++ = 0x48; *q++ = 0x83; *q++ = 0xC4; *q++ = 0x20;
  return gfxopt::EmitJmpAbs(q, back);
}

static void Install(HMODULE hExe) {
  uint8_t *exe = (uint8_t *)hExe;
  uint8_t *input = exe + 0x00409B9D;  // call 3CD100 ... call 1AAEF0 (0x27 bytes) -> 0x409BC4
  uint8_t *ctor = exe + 0x003FC7B1;   // call 3CD100 ... call 1AAE00 ; nop (0x1D bytes) -> 0x3FC7CE
  static const uint8_t kInput[] = {0xE8, 0x5E, 0x35, 0xFC, 0xFF, 0x48, 0x8B, 0xC8, 0xE8, 0x66, 0x3B, 0xFC, 0xFF,
                                   0x80, 0x78, 0x24, 0x00, 0x0F, 0x94, 0xC1, 0x33, 0xD2, 0x84, 0xC9, 0x88, 0x48,
                                   0x24, 0x48, 0x8D, 0x4B, 0x28, 0x0F, 0x95, 0xC2, 0xE8, 0x2C, 0x13, 0xDA, 0xFF};
  static const uint8_t kCtor[] = {0xE8, 0x4A, 0x09, 0xFD, 0xFF, 0x48, 0x8B, 0xC8, 0xE8, 0x52, 0x0F, 0xFD, 0xFF, 0x80, 0x78,
                                  0x24, 0x00, 0x74, 0x0A, 0x48, 0x8D, 0x4F, 0x28, 0xE8, 0x33, 0xE6, 0xDA, 0xFF, 0x90};
  if (memcmp(input, kInput, sizeof(kInput)) != 0 || memcmp(ctor, kCtor, sizeof(kCtor)) != 0) {
    log_line("[camera] WARNING: unexpected camera code; hood/cockpit cameras not added\r\n");
    return;
  }
  uint8_t *mem = (uint8_t *)AllocateNearAddress(exe + 0x00400000, 0x1000);
  if (!mem) { log_line("[camera] WARNING: no memory near the exe; hood/cockpit cameras not added\r\n"); return; }
  DWORD old = 0;
  VirtualProtect(mem, 0x1000, PAGE_EXECUTE_READWRITE, &old);

  if (gfxopt::g_ini[0]) {
    UINT c = GetPrivateProfileIntW(L"Game", L"Camera", (UINT)-1, gfxopt::g_ini);
    if (c <= 4) g_choice = (LONG)c;
  }

  uint8_t *stubInput = mem;
  uint8_t *stubCtor = EmitCallStub(stubInput, 0xD9, (const void *)&OnChangeCamera, exe + 0x00409BC4);
  EmitCallStub(stubCtor, 0xF9, (const void *)&OnCamerasModeCreated, exe + 0x003FC7CE);
  FlushInstructionCache(GetCurrentProcess(), mem, 0x100);
  if (gfxopt::WriteJmpRel(input, stubInput, sizeof(kInput)) && gfxopt::WriteJmpRel(ctor, stubCtor, sizeof(kCtor)))
    log_line("[camera] change camera cycles far / near / hood / cockpit (saved camera %ld)\r\n", (long)g_choice);
}

}  // namespace cams
