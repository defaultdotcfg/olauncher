// Language rows on the first page of the game's Options (after "Button prompts"), text switched live.
//
// The game loads its texts once at boot (exe+0x2ED9CF): exe+0x22E4B0 loads
// code:localisation/xbox/<lang>.loc through exe+0x22E140(asset registry [exe+0x1681D38]), the language
// coming from GetUserDefaultLocaleName (kernelx honours ONRUSH_TEXT_LANG), and stores the database in
// [exe+0x1681D30], which every lookup (exe+0x22DFA0) reads. Right after, it caches the decimal separator
// (exe+0x22E430) and the time formats (exe+0x22E420 date, exe+0x22E4F0 the eight ID_TIME_* strings).
// Changing the language redoes exactly those steps with the new locale; the previous database stays
// loaded (texts already handed out keep pointing into it), screens built afterwards use the new one.
namespace lang {

static const wchar_t *const kLocale[] = {L"en-US", L"pt-BR", L"es-ES", L"fr-FR", L"de-DE", L"it-IT", L"pl-PL"};
static const char *const kTextKeys[] = {"ID_RL_LANG_EN", "ID_RL_LANG_PT", "ID_RL_LANG_ES", "ID_RL_LANG_FR",
                                        "ID_RL_LANG_DE", "ID_RL_LANG_IT", "ID_RL_LANG_PL"};
static const char *const kVoiceKeys[] = {"ID_RL_SAME_AS_TEXT", "ID_RL_LANG_EN", "ID_RL_LANG_PT", "ID_RL_LANG_ES",
                                         "ID_RL_LANG_FR", "ID_RL_LANG_DE", "ID_RL_LANG_IT", "ID_RL_LANG_PL"};
static const int kCount = 7;
enum : uint32_t { kIdText = 0x60, kIdVoice = 0x61 };

static volatile LONG g_text = 0;   // index into kLocale
static volatile LONG g_voice = 0;  // 0 = same as text, else kLocale[g_voice - 1]

static int LocaleIndex(const wchar_t *loc) {
  for (int i = 0; i < kCount; ++i)
    if (_wcsnicmp(loc, kLocale[i], 2) == 0) return i;
  return -1;
}

static void LoadChoices() {
  wchar_t loc[LOCALE_NAME_MAX_LENGTH] = {};
  DWORD n = GetEnvironmentVariableW(L"ONRUSH_TEXT_LANG", loc, LOCALE_NAME_MAX_LENGTH);
  if (n == 0 || n >= LOCALE_NAME_MAX_LENGTH) GetUserDefaultLocaleName(loc, LOCALE_NAME_MAX_LENGTH);
  int i = LocaleIndex(loc);
  g_text = i < 0 ? 0 : i;
  n = GetEnvironmentVariableW(L"ONRUSH_VOICE_LANG", loc, LOCALE_NAME_MAX_LENGTH);
  i = (n > 0 && n < LOCALE_NAME_MAX_LENGTH) ? LocaleIndex(loc) : -1;
  g_voice = i < 0 ? 0 : i + 1;
}

// Reloads the texts for the current GetUserDefaultLocaleName (see the header).
static bool ReloadTexts() {
  uint8_t *exe = (uint8_t *)gfxopt::g_exe;
  void *registry = *(void **)(exe + 0x01681D38);
  if (!registry) return false;
  typedef void *(*PFN_Load)(void *);
  void *db = ((PFN_Load)(exe + 0x0022E140))(registry);
  if (!db) return false;
  *(void *volatile *)(exe + 0x01681D30) = db;

  typedef const char *(*PFN_Loc)(const char *);
  PFN_Loc L = (PFN_Loc)(exe + 0x0022DFA0);
  ((void (*)())(exe + 0x0022E430))();                                  // decimal separator
  ((void (*)(const char *))(exe + 0x0022E420))(L("ID_TIME_DATE"));
  typedef void (*PFN_Times)(const char *, const char *, const char *, const char *, const char *, const char *,
                            const char *, const char *);
  ((PFN_Times)(exe + 0x0022E4F0))(L("ID_TIME_DAYS_HOURS"), L("ID_TIME_DAYS_HOURS_MINUTES"),
                                  L("ID_TIME_HRS_MINS_SEC"), L("ID_TIME_HRS_MINS"), L("ID_TIME_MINS_SEC"),
                                  L("ID_TIME_SEC"), L("ID_TIME_SEC_TENTH"), L("ID_TIME_MIN_SEC_TENTH"));
  return true;
}

static void OnText(float value, int) {
  LONG v = gfxopt::ListIndex(value, kCount);
  if (v == g_text) return;
  g_text = v;
  SetEnvironmentVariableW(L"ONRUSH_TEXT_LANG", kLocale[v]);
  gfxopt::s_lang = -1;
  bool ok = ReloadTexts();
  if (gfxopt::g_ini[0]) WritePrivateProfileStringW(L"Language", L"Text", kLocale[v], gfxopt::g_ini);
  log_line("[lang] text language -> %ls (%s)\r\n", kLocale[v], ok ? "reloaded" : "RELOAD FAILED");
}

static void OnVoice(float value, int) {
  LONG v = gfxopt::ListIndex(value, kCount + 1);
  if (v == g_voice) return;
  g_voice = v;
  if (gfxopt::g_ini[0]) WritePrivateProfileStringW(L"Language", L"Voice", v ? kLocale[v - 1] : L"", gfxopt::g_ini);
  log_line("[lang] voice language -> %ls (next start)\r\n", v ? kLocale[v - 1] : L"same as text");
}

static void __cdecl AddRows(void *page) {
  typedef void (*PFN_AddItem)(void *page, const gfxopt::MenuItem *item);
  PFN_AddItem add = (PFN_AddItem)((uint8_t *)gfxopt::g_exe + 0x00340DB0);
  gfxopt::MenuItem it = {};
  it.type = 6;
  it.invoker = (uint8_t *)gfxopt::g_exe + 0x0035E960;
  it.name = "ID_RL_LANG"; it.callback = (void *)&OnText; it.id = kIdText;
  it.values = kTextKeys; it.count = kCount; it.value = (float)g_text;
  add(page, &it);
  it.name = "ID_RL_VOICE"; it.callback = (void *)&OnVoice; it.id = kIdVoice;
  it.values = kVoiceKeys; it.count = kCount + 1; it.value = (float)g_voice;
  add(page, &it);
}

static void Install(HMODULE hExe) {
  uint8_t *exe = (uint8_t *)hExe;
  // Options builder, end of the first page: `mov rbx, [rsi+0x2E0]` (rbx = that page until here).
  uint8_t *site = exe + 0x003550A4;
  static const uint8_t kSite[] = {0x48, 0x8B, 0x9E, 0xE0, 0x02, 0x00, 0x00};
  if (memcmp(site, kSite, sizeof(kSite)) != 0) {
    log_line("[lang] WARNING: unexpected options builder code; language rows not added\r\n");
    return;
  }
  LoadChoices();
  uint8_t *mem = (uint8_t *)AllocateNearAddress(site, 0x100);
  if (!mem) return;
  DWORD old = 0;
  VirtualProtect(mem, 0x100, PAGE_EXECUTE_READWRITE, &old);
  uint8_t *q = mem;
  *q++ = 0x48; *q++ = 0x83; *q++ = 0xEC; *q++ = 0x20;                                     // sub rsp,20h
  *q++ = 0x48; *q++ = 0x89; *q++ = 0xD9;                                                  // mov rcx,rbx
  *q++ = 0x48; *q++ = 0xB8; { void *f = (void *)&AddRows; memcpy(q, &f, 8); q += 8; }     // mov rax,AddRows
  *q++ = 0xFF; *q++ = 0xD0;                                                               // call rax
  *q++ = 0x48; *q++ = 0x83; *q++ = 0xC4; *q++ = 0x20;                                     // add rsp,20h
  memcpy(q, kSite, sizeof(kSite)); q += sizeof(kSite);                                    // mov rbx,[rsi+2E0h]
  q = gfxopt::EmitJmpAbs(q, site + sizeof(kSite));
  FlushInstructionCache(GetCurrentProcess(), mem, (size_t)(q - mem));
  if (gfxopt::WriteJmpRel(site, mem, sizeof(kSite)))
    log_line("[lang] language rows added (text %ls, voice %ls)\r\n", kLocale[g_text],
             g_voice ? kLocale[g_voice - 1] : L"same as text");
}

}  // namespace lang
