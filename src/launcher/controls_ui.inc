// ============================================================================
// "Controles" tab: which keyboard keys play as the Xbox controller in game.
// Actions, ini names and defaults live in src/d3d12_x/keymap_defs.h (shared with the bridge, which reads
// ONRUSH_KEYMAP in PollInput). Saved in onrush.ini [Keyboard]; the game picks the map up on the next JOGAR.
// Each action has two keys; a key may serve several actions (W accelerates and also moves up in the menus).
// ============================================================================
#include "../d3d12_x/keymap_defs.h"

#define ID_TAB_CONTROLS  1100
#define ID_KB_LIST       1101
#define ID_KB_KEY1       1102
#define ID_KB_KEY2       1103
#define ID_KB_CLEAR      1104
#define ID_KB_DEFAULTS   1105
#define KB_TIMER_ID      0x7710
#define KB_CAPTURE_MS    10000

static HWND g_hTabControls = nullptr, g_hKbList = nullptr, g_hKbKey1 = nullptr, g_hKbKey2 = nullptr, g_hKbClear = nullptr,
            g_hKbDefaults = nullptr;
static unsigned char g_km[KM_COUNT][KM_SLOTS];
static int g_kbCapSlot = -1;          // slot being captured (-1: none)
static int g_kbCapAction = -1;
static bool g_kbCapWaitRelease = false;
static ULONGLONG g_kbCapStart = 0;
static int g_kbStatus = 0;            // panel message: 0 none, 1 capturing, 2 reserved key, 3 saved, 4 defaults restored
static int g_kbStatusKey = 0;

enum KbStr { K_TAB, K_TITLE, K_HINT, K_COL_ACTION, K_COL_KEY1, K_COL_KEY2, K_SELECTED, K_KEY1, K_KEY2, K_CLEAR, K_DEFAULTS,
             K_CANCEL, K_CAPTURE_FMT, K_RESERVED_FMT, K_SAVED, K_RESTORED, K_CONFIRM_DEF, K_NOTE, K_NONE, K_PICK, K_COUNT };
static const wchar_t *kKbStr[K_COUNT][3] = {
    { L"Controles", L"Controls", L"Controles" },
    { L"CONTROLES", L"CONTROLS", L"CONTROLES" },
    { L"O teclado funciona como um controle de Xbox, junto com o controle de verdade. Escolha uma ação e troque as teclas. Cima, baixo, esquerda e direita movem o analógico esquerdo. O direcional (provocações na corrida) tem teclas próprias.",
      L"The keyboard works as an Xbox controller, alongside a real one. Pick an action and change its keys. Up, down, left and right move the left stick. The D-pad (taunts in a race) has its own keys.",
      L"El teclado funciona como un mando de Xbox, junto con el mando real. Elige una acción y cambia sus teclas. Arriba, abajo, izquierda y derecha mueven el stick izquierdo. La cruceta (provocaciones en la carrera) tiene sus propias teclas." },
    { L"Ação", L"Action", L"Acción" },
    { L"Tecla 1", L"Key 1", L"Tecla 1" },
    { L"Tecla 2", L"Key 2", L"Tecla 2" },
    { L"Ação escolhida", L"Selected action", L"Acción elegida" },
    { L"Trocar tecla 1", L"Change key 1", L"Cambiar tecla 1" },
    { L"Trocar tecla 2", L"Change key 2", L"Cambiar tecla 2" },
    { L"Limpar as teclas da ação", L"Clear the action's keys", L"Borrar las teclas de la acción" },
    { L"Restaurar o padrão", L"Restore defaults", L"Restaurar valores predeterminados" },
    { L"Cancelar", L"Cancel", L"Cancelar" },
    { L"Aperte a tecla para \"%ls\"…", L"Press the key for \"%ls\"…", L"Pulsa la tecla para \"%ls\"…" },
    { L"%ls é usada pelo jogo (fotos e capturas). Escolha outra tecla.", L"%ls is used by the game (photos and captures). Pick another key.",
      L"%ls la usa el juego (fotos y capturas). Elige otra tecla." },
    { L"Salvo. Vale a partir do próximo JOGAR.", L"Saved. Takes effect on the next PLAY.", L"Guardado. Se aplica en el próximo JUGAR." },
    { L"Teclas padrão restauradas.", L"Default keys restored.", L"Teclas predeterminadas restauradas." },
    { L"Voltar todas as teclas para o padrão?", L"Set every key back to the default?", L"¿Volver todas las teclas a los valores predeterminados?" },
    { L"As teclas só valem com a janela do jogo em foco. F11, F12, Alt+Enter e Alt+F4 ficam com o jogo.",
      L"Keys only work while the game window is focused. F11, F12, Alt+Enter and Alt+F4 stay with the game.",
      L"Las teclas solo funcionan con la ventana del juego activa. F11, F12, Alt+Enter y Alt+F4 quedan para el juego." },
    { L"—", L"—", L"—" },
    { L"Escolha uma ação na lista primeiro.", L"Pick an action in the list first.", L"Elige una acción en la lista primero." },
};
static const wchar_t *KT(KbStr s) { return kKbStr[s][g_uiLang]; }

static const wchar_t *kKbActionName[KM_COUNT][3] = {
    { L"Acelerar (RT)", L"Accelerate (RT)", L"Acelerar (RT)" },
    { L"Frear e ré (LT)", L"Brake and reverse (LT)", L"Frenar y reversa (LT)" },
    { L"Esquerda", L"Left", L"Izquierda" },
    { L"Direita", L"Right", L"Derecha" },
    { L"Cima", L"Up", L"Arriba" },
    { L"Baixo", L"Down", L"Abajo" },
    { L"Direcional cima", L"D-pad up", L"Cruceta arriba" },
    { L"Direcional baixo", L"D-pad down", L"Cruceta abajo" },
    { L"Direcional esquerda", L"D-pad left", L"Cruceta izquierda" },
    { L"Direcional direita", L"D-pad right", L"Cruceta derecha" },
    { L"Botão A", L"A button", L"Botón A" },
    { L"Botão B", L"B button", L"Botón B" },
    { L"Botão X", L"X button", L"Botón X" },
    { L"Botão Y", L"Y button", L"Botón Y" },
    { L"LB", L"LB", L"LB" },
    { L"RB", L"RB", L"RB" },
    { L"View", L"View", L"View" },
    { L"Menu", L"Menu", L"Menú" },
    { L"L3 (clique analóg. esq.)", L"L3 (left stick click)", L"L3 (clic stick izq.)" },
    { L"R3 (clique analóg. dir.)", L"R3 (right stick click)", L"R3 (clic stick der.)" },
    { L"Analógico dir. esquerda", L"Right stick left", L"Stick der. izquierda" },
    { L"Analógico dir. direita", L"Right stick right", L"Stick der. derecha" },
    { L"Analógico dir. cima", L"Right stick up", L"Stick der. arriba" },
    { L"Analógico dir. baixo", L"Right stick down", L"Stick der. abajo" },
};

// Readable name of a virtual key.
static std::wstring KbKeyName(int vk)
{
    if (!vk) return KT(K_NONE);
    static const wchar_t *space[3] = { L"Espaço", L"Space", L"Espacio" };
    static const wchar_t *side[2][3] = { { L"esq.", L"left", L"izq." }, { L"dir.", L"right", L"der." } };
    wchar_t t[64];
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) { t[0] = (wchar_t)vk; t[1] = 0; return t; }
    if (vk >= VK_F1 && vk <= VK_F24) { swprintf_s(t, L"F%d", vk - VK_F1 + 1); return t; }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) { swprintf_s(t, L"Num %d", vk - VK_NUMPAD0); return t; }
    if (vk >= VK_LSHIFT && vk <= VK_RMENU)
    {
        static const wchar_t *base[3] = { L"Shift", L"Ctrl", L"Alt" };
        swprintf_s(t, L"%ls %ls", base[(vk - VK_LSHIFT) / 2], side[(vk - VK_LSHIFT) & 1][g_uiLang]);
        return t;
    }
    switch (vk)
    {
    case VK_BACK: return L"Backspace";
    case VK_TAB: return L"Tab";
    case VK_RETURN: return L"Enter";
    case VK_ESCAPE: return L"Esc";
    case VK_SPACE: return space[g_uiLang];
    case VK_PRIOR: return L"Page Up";
    case VK_NEXT: return L"Page Down";
    case VK_END: return L"End";
    case VK_HOME: return L"Home";
    case VK_LEFT: return L"←";
    case VK_UP: return L"↑";
    case VK_RIGHT: return L"→";
    case VK_DOWN: return L"↓";
    case VK_INSERT: return L"Insert";
    case VK_DELETE: return L"Delete";
    case VK_CAPITAL: return L"Caps Lock";
    case VK_PAUSE: return L"Pause";
    case VK_MULTIPLY: return L"Num *";
    case VK_ADD: return L"Num +";
    case VK_SUBTRACT: return L"Num -";
    case VK_DECIMAL: return L"Num ,";
    case VK_DIVIDE: return L"Num /";
    }
    UINT sc = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC);
    if (sc && GetKeyNameTextW((LONG)(sc << 16), t, 64) > 0) return t;
    swprintf_s(t, L"0x%02X", vk);
    return t;
}

static std::wstring KbIniName(int a)
{
    std::wstring w;
    for (const char *p = kKeymapDefaults[a].ini; *p; ++p) w.push_back((wchar_t)*p);
    return w;
}

static std::wstring KbIniPath() { wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%ls\\onrush.ini", g_exeDir); return p; }

// onrush.ini [Keyboard] Accelerate=87,38 (decimal VK codes; empty = no key; missing = default)
static void KbLoadInto(unsigned char km[KM_COUNT][KM_SLOTS])
{
    std::wstring ini = KbIniPath();
    for (int a = 0; a < KM_COUNT; ++a)
    {
        for (int s = 0; s < KM_SLOTS; ++s) km[a][s] = kKeymapDefaults[a].keys[s];
        wchar_t v[64];
        GetPrivateProfileStringW(L"Keyboard", KbIniName(a).c_str(), L"?", v, 64, ini.c_str());
        if (!wcscmp(v, L"?")) continue;
        unsigned char k[KM_SLOTS] = {};
        int n = 0;
        for (wchar_t *p = v; *p && n < KM_SLOTS;)
        {
            wchar_t *end = nullptr;
            long x = wcstol(p, &end, 10);
            if (end == p) break;
            if (x > 0 && x < 0xFF && !KeymapReserved((int)x)) k[n++] = (unsigned char)x;
            if (*end != L',') break;
            p = end + 1;
        }
        for (int s = 0; s < KM_SLOTS; ++s) km[a][s] = k[s];
    }
}

static void KbLoad() { KbLoadInto(g_km); }

static void KbSave()
{
    std::wstring ini = KbIniPath();
    for (int a = 0; a < KM_COUNT; ++a)
    {
        wchar_t v[32] = {};
        for (int s = 0; s < KM_SLOTS; ++s)
            if (g_km[a][s]) swprintf_s(v + wcslen(v), 32 - wcslen(v), L"%ls%d", v[0] ? L"," : L"", g_km[a][s]);
        WritePrivateProfileStringW(L"Keyboard", KbIniName(a).c_str(), v, ini.c_str());
    }
}

// GameSupervisorThread (worker thread, so it reads its own copy): hands the map to the game
// (bridge PollInput reads ONRUSH_KEYMAP).
static void KbSetGameEnv()
{
    unsigned char km[KM_COUNT][KM_SLOTS];
    KbLoadInto(km);
    std::wstring env;
    for (int a = 0; a < KM_COUNT; ++a)
    {
        wchar_t part[64];
        swprintf_s(part, L"%ls%ls=", env.empty() ? L"" : L";", KbIniName(a).c_str());
        env += part;
        bool first = true;
        for (int s = 0; s < KM_SLOTS; ++s)
            if (km[a][s]) { swprintf_s(part, L"%ls%X", first ? L"" : L",", km[a][s]); env += part; first = false; }
    }
    SetEnvironmentVariableW(L"ONRUSH_KEYMAP", env.c_str());
}

static int KbSelectedRow() { return g_hKbList ? ListView_GetNextItem(g_hKbList, -1, LVNI_SELECTED) : -1; }

static void KbRefreshList()
{
    if (!g_hKbList) return;
    // rows are created once and only their texts change, so the selection (and a running capture) survive
    SendMessageW(g_hKbList, WM_SETREDRAW, FALSE, 0);
    for (int a = 0; a < KM_COUNT; ++a)
    {
        LVITEMW it = {};
        it.mask = LVIF_TEXT;
        it.iItem = a;
        it.pszText = (LPWSTR)kKbActionName[a][g_uiLang];
        if (ListView_GetItemCount(g_hKbList) <= a) SendMessageW(g_hKbList, LVM_INSERTITEMW, 0, (LPARAM)&it);
        else SendMessageW(g_hKbList, LVM_SETITEMTEXTW, a, (LPARAM)&it);
        for (int s = 0; s < KM_SLOTS; ++s)
        {
            std::wstring n = (g_kbCapSlot == s && g_kbCapAction == a) ? L"…" : KbKeyName(g_km[a][s]);
            LVITEMW sub = {};
            sub.iSubItem = 1 + s;
            sub.pszText = (LPWSTR)n.c_str();
            SendMessageW(g_hKbList, LVM_SETITEMTEXTW, a, (LPARAM)&sub);
        }
    }
    SendMessageW(g_hKbList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hKbList, nullptr, TRUE);
}

static void KbInvalidatePanel(HWND hwnd)
{
    RECT r = { UI_PANEL_X, 130, UI_PANEL_X + UI_PANEL_W, UI_H - 60 };
    InvalidateRect(hwnd, &r, FALSE);
}

static void KbApplyLanguage()
{
    SetWindowTextW(g_hTabControls, KT(K_TAB));
    SetWindowTextW(g_hKbKey1, g_kbCapSlot == 0 ? KT(K_CANCEL) : KT(K_KEY1));
    SetWindowTextW(g_hKbKey2, g_kbCapSlot == 1 ? KT(K_CANCEL) : KT(K_KEY2));
    SetWindowTextW(g_hKbClear, KT(K_CLEAR));
    SetWindowTextW(g_hKbDefaults, KT(K_DEFAULTS));
    KbRefreshList();
}

static void KbEndCapture(HWND hwnd)
{
    if (g_kbCapSlot < 0) return;
    KillTimer(hwnd, KB_TIMER_ID);
    g_kbCapSlot = -1; g_kbCapAction = -1;
    if (g_kbStatus == 1) g_kbStatus = 0;
    KbApplyLanguage();
    KbInvalidatePanel(hwnd);
}

static void KbBeginCapture(HWND hwnd, int slot)
{
    int row = KbSelectedRow();
    if (row < 0) { MessageBoxW(hwnd, KT(K_PICK), L"ONRUSH", MB_ICONINFORMATION); return; }
    if (g_kbCapSlot == slot) { KbEndCapture(hwnd); return; }  // the button reads "Cancelar" while capturing
    KbEndCapture(hwnd);
    g_kbCapSlot = slot; g_kbCapAction = row;
    g_kbCapWaitRelease = true;          // the key that clicked the button (Enter/Space) must not be taken
    g_kbCapStart = GetTickCount64();
    g_kbStatus = 1;
    SetFocus(hwnd);                     // keys go to the main window, which ignores them (no button gets "clicked")
    SetTimer(hwnd, KB_TIMER_ID, 30, nullptr);
    KbApplyLanguage();
    KbInvalidatePanel(hwnd);
}

// Keys a capture can take: keyboard only (no mouse buttons), the left/right variants of Shift/Ctrl/Alt
// instead of the generic codes, no Windows keys.
static bool KbCapturable(int vk)
{
    if (vk < 0x08 || vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) return false;
    if (vk >= 0x0A && vk <= 0x0C) return false;
    if (vk >= 0xE5 || (vk >= 0xA6 && vk <= 0xB7)) return false;  // IME/processing, browser and media keys
    return true;
}

static void KbOnTimer(HWND hwnd)
{
    if (g_kbCapSlot < 0) { KillTimer(hwnd, KB_TIMER_ID); return; }
    if (GetTickCount64() - g_kbCapStart > KB_CAPTURE_MS) { KbEndCapture(hwnd); return; }
    if (GetForegroundWindow() != hwnd) return;
    int pressed = 0;
    for (int vk = 0x08; vk < 0xFF && !pressed; ++vk)
        if (KbCapturable(vk) && (GetAsyncKeyState(vk) & 0x8000)) pressed = vk;
    if (g_kbCapWaitRelease) { if (!pressed) g_kbCapWaitRelease = false; return; }
    if (!pressed) return;
    if (KeymapReserved(pressed))
    {
        g_kbStatus = 2; g_kbStatusKey = pressed;
        g_kbCapWaitRelease = true;
        KbInvalidatePanel(hwnd);
        return;
    }
    int a = g_kbCapAction, s = g_kbCapSlot;
    g_km[a][s] = (unsigned char)pressed;
    for (int o = 0; o < KM_SLOTS; ++o) if (o != s && g_km[a][o] == pressed) g_km[a][o] = 0;  // no key twice in one action
    KbSave();
    LogPrintf(L"[Controles] %ls: tecla %d = %ls\r\n", kKbActionName[a][0], s + 1, KbKeyName(pressed).c_str());
    g_kbStatus = 3;
    KbEndCapture(hwnd);
    SetFocus(g_hKbList);
}

static void KbApplyThemeColors()
{
    if (!g_hKbList) return;
    ListView_SetBkColor(g_hKbList, g_pal.heroDim);
    ListView_SetTextBkColor(g_hKbList, g_pal.heroDim);
    ListView_SetTextColor(g_hKbList, COLOR_TEXT_PRIMARY);
}

static void KbCreateControls(HWND hwnd)
{
    const int X = UI_PANEL_X, W = UI_PANEL_W;
    DWORD od = WS_CHILD | WS_TABSTOP | BS_OWNERDRAW;
    g_hTabControls = CreateWindowW(L"BUTTON", L"", od | WS_VISIBLE, 16, PLAYLIST_HIDDEN ? 324 : 372, 184, 42, hwnd, (HMENU)ID_TAB_CONTROLS, nullptr, nullptr);
    g_hKbList = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER | LVS_NOSORTHEADER,
                                232, 96, UI_HERO_W - 248, UI_H - 116, hwnd, (HMENU)ID_KB_LIST, nullptr, nullptr);
    SetWindowTheme(g_hKbList, g_pal.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    ListView_SetExtendedListViewStyle(g_hKbList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    KbApplyThemeColors();
    const int listW = UI_HERO_W - 248, keyW = 92;
    LVCOLUMNW c = {};
    c.mask = LVCF_WIDTH | LVCF_SUBITEM;
    c.cx = listW - 2 * keyW - 4; c.iSubItem = 0; SendMessageW(g_hKbList, LVM_INSERTCOLUMNW, 0, (LPARAM)&c);
    c.cx = keyW; c.iSubItem = 1; SendMessageW(g_hKbList, LVM_INSERTCOLUMNW, 1, (LPARAM)&c);
    c.cx = keyW; c.iSubItem = 2; SendMessageW(g_hKbList, LVM_INSERTCOLUMNW, 2, (LPARAM)&c);
    g_hKbKey1 = CreateWindowW(L"BUTTON", L"", od, X, 222, W, 32, hwnd, (HMENU)ID_KB_KEY1, nullptr, nullptr);
    g_hKbKey2 = CreateWindowW(L"BUTTON", L"", od, X, 262, W, 32, hwnd, (HMENU)ID_KB_KEY2, nullptr, nullptr);
    g_hKbClear = CreateWindowW(L"BUTTON", L"", od, X, 302, W, 32, hwnd, (HMENU)ID_KB_CLEAR, nullptr, nullptr);
    g_hKbDefaults = CreateWindowW(L"BUTTON", L"", od, X, 358, W, 32, hwnd, (HMENU)ID_KB_DEFAULTS, nullptr, nullptr);
    KbLoad();
    KbRefreshList();
    ListView_SetItemState(g_hKbList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

static void KbUpdateVisibility(HWND hwnd)
{
    bool on = g_tab == 5;
    if (!on) KbEndCapture(hwnd);
    HWND c[] = { g_hKbList, g_hKbKey1, g_hKbKey2, g_hKbClear, g_hKbDefaults };
    for (HWND h : c) if (h) ShowWindow(h, on ? SW_SHOW : SW_HIDE);
    if (on) { KbLoad(); g_kbStatus = 0; KbApplyLanguage(); }
    InvalidateRect(g_hTabControls, nullptr, TRUE);
}

// Left area: title and column names over the list.
static void KbDrawHero(Gdiplus::Graphics &g)
{
    Gdiplus::SolidBrush dim(GPC(g_pal.heroDim));
    g.FillRectangle(&dim, 216, 0, UI_HERO_W - 216, UI_H);
    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font fTitle(&ff, 22, Gdiplus::FontStyleBold, Gdiplus::UnitPixel), fHead(&ff, 12, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush white(GPC(COLOR_TEXT_PRIMARY)), muted(GPC(COLOR_TEXT_MUTED));
    g.DrawString(KT(K_TITLE), -1, &fTitle, Gdiplus::PointF(232, 18), &white);
    const int listW = UI_HERO_W - 248, keyW = 92;
    float x0 = 232 + 6, x1 = 232.f + (listW - 2 * keyW - 4) + 6, x2 = x1 + keyW;
    g.DrawString(KT(K_COL_ACTION), -1, &fHead, Gdiplus::PointF(x0, 72), &muted);
    g.DrawString(KT(K_COL_KEY1), -1, &fHead, Gdiplus::PointF(x1, 72), &muted);
    g.DrawString(KT(K_COL_KEY2), -1, &fHead, Gdiplus::PointF(x2, 72), &muted);
    Gdiplus::Pen line(GPC(COLOR_BORDER));
    g.DrawLine(&line, 232, 91, UI_HERO_W - 16, 91);
}

static void KbDrawPanel(HDC hdc)
{
    const int X = UI_PANEL_X, W = UI_PANEL_W;
    SelectObject(hdc, g_hFontPlay); SetTextColor(hdc, COLOR_TEXT_PRIMARY);
    TextOutW(hdc, X, 20, KT(K_TITLE), (int)wcslen(KT(K_TITLE)));
    SelectObject(hdc, g_hFontUI); SetTextColor(hdc, COLOR_TEXT_MUTED);
    RECT hint = { X, 54, X + W, 164 };
    DrawTextW(hdc, KT(K_HINT), -1, &hint, DT_LEFT | DT_WORDBREAK);
    int row = KbSelectedRow();
    if (row >= 0)
    {
        SetTextColor(hdc, COLOR_TEXT_MUTED);
        TextOutW(hdc, X, 170, KT(K_SELECTED), (int)wcslen(KT(K_SELECTED)));
        SelectObject(hdc, g_hFontUIBold); SetTextColor(hdc, COLOR_TEXT_PRIMARY);
        TextOutW(hdc, X, 190, kKbActionName[row][g_uiLang], (int)wcslen(kKbActionName[row][g_uiLang]));
        SelectObject(hdc, g_hFontUI);
    }
    wchar_t msg[256] = {};
    COLORREF col = COLOR_TEXT_PRIMARY;
    if (g_kbStatus == 1 && g_kbCapAction >= 0) { swprintf_s(msg, KT(K_CAPTURE_FMT), kKbActionName[g_kbCapAction][g_uiLang]); col = COLOR_ACCENT_ORANGE; }
    else if (g_kbStatus == 2) { swprintf_s(msg, KT(K_RESERVED_FMT), KbKeyName(g_kbStatusKey).c_str()); col = COLOR_ACCENT_RED; }
    else if (g_kbStatus == 3) { wcscpy_s(msg, KT(K_SAVED)); col = COLOR_ACCENT_GREEN; }
    else if (g_kbStatus == 4) { wcscpy_s(msg, KT(K_RESTORED)); col = COLOR_ACCENT_GREEN; }
    if (msg[0])
    {
        SetTextColor(hdc, col);
        RECT m = { X, 404, X + W, 460 };
        DrawTextW(hdc, msg, -1, &m, DT_LEFT | DT_WORDBREAK);
    }
    SetTextColor(hdc, COLOR_TEXT_MUTED);
    RECT note = { X, 470, X + W, 540 };
    DrawTextW(hdc, KT(K_NOTE), -1, &note, DT_LEFT | DT_WORDBREAK);
}

static bool KbHandleCommand(HWND hwnd, int id, int code)
{
    (void)code;
    switch (id)
    {
    case ID_TAB_CONTROLS: PlSetTab(hwnd, 5); return true;
    case ID_KB_KEY1: KbBeginCapture(hwnd, 0); return true;
    case ID_KB_KEY2: KbBeginCapture(hwnd, 1); return true;
    case ID_KB_CLEAR:
    {
        int row = KbSelectedRow();
        if (row < 0) { MessageBoxW(hwnd, KT(K_PICK), L"ONRUSH", MB_ICONINFORMATION); return true; }
        KbEndCapture(hwnd);
        for (int s = 0; s < KM_SLOTS; ++s) g_km[row][s] = 0;
        KbSave();
        g_kbStatus = 3;
        KbRefreshList();
        KbInvalidatePanel(hwnd);
        return true;
    }
    case ID_KB_DEFAULTS:
        KbEndCapture(hwnd);
        if (MessageBoxW(hwnd, KT(K_CONFIRM_DEF), L"ONRUSH", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES) return true;
        for (int a = 0; a < KM_COUNT; ++a)
            for (int s = 0; s < KM_SLOTS; ++s) g_km[a][s] = kKeymapDefaults[a].keys[s];
        KbSave();
        g_kbStatus = 4;
        KbRefreshList();
        KbInvalidatePanel(hwnd);
        return true;
    }
    return false;
}

static bool KbHandleNotify(HWND hwnd, NMHDR *nh)
{
    if (!g_hKbList || nh->hwndFrom != g_hKbList) return false;
    if (nh->code == NM_DBLCLK)
    {
        int item = ((NMITEMACTIVATE *)nh)->iItem, sub = ((NMITEMACTIVATE *)nh)->iSubItem;
        if (item >= 0) KbBeginCapture(hwnd, sub == 2 ? 1 : 0);   // double-click on the "Tecla 2" column changes key 2
    }
    else if (nh->code == LVN_ITEMCHANGED && (((NMLISTVIEW *)nh)->uChanged & LVIF_STATE))
    {
        if (g_kbCapSlot >= 0 && KbSelectedRow() != g_kbCapAction) KbEndCapture(hwnd);
        if (g_kbStatus != 1) g_kbStatus = 0;
        KbInvalidatePanel(hwnd);
    }
    return true;
}
